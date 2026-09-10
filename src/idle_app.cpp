#include "idle_app.hpp"
#include "process.hpp"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/inotify.h>
#include <signal.h>
#include <filesystem>

namespace miquidle {

static const struct wl_registry_listener registry_listener = {
    .global = IdleApp::handle_global,
    .global_remove = IdleApp::handle_global_remove,
};

static const struct ext_idle_notification_v1_listener idle_notification_listener = {
    .idled = IdleApp::handle_idled,
    .resumed = IdleApp::handle_resumed,
};

IdleApp::IdleApp(Config config, std::string config_path)
    : m_config(std::move(config)), m_config_path(std::move(config_path)) {}

IdleApp::~IdleApp() {
    stop();
    clear_idle_listeners();

    if (m_idle_notifier) {
        ext_idle_notifier_v1_destroy(m_idle_notifier);
        m_idle_notifier = nullptr;
    }

    if (m_seat) {
        wl_seat_destroy(m_seat);
        m_seat = nullptr;
    }

    if (m_registry) {
        wl_registry_destroy(m_registry);
        m_registry = nullptr;
    }

    if (m_display) {
        wl_display_disconnect(m_display);
        m_display = nullptr;
    }

    cleanup_inotify();
    cleanup_signals();
}

bool IdleApp::init() {
    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        std::cerr << "[miquidle] Error: Cannot connect to Wayland display. Is a compositor running?\n";
        return false;
    }

    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &registry_listener, this);

    wl_display_roundtrip(m_display);

    if (!m_idle_notifier) {
        std::cerr << "[miquidle] Error: Compositor does not support 'ext_idle_notifier_v1'.\n";
        return false;
    }

    if (!m_seat) {
        std::cerr << "[miquidle] Error: No Wayland seat found.\n";
        return false;
    }

    setup_signals();
    setup_inotify();
    setup_idle_listeners();
    setup_dbus();

    return true;
}

void IdleApp::clear_idle_listeners() {
    for (auto& n : m_notifications) {
        if (n->notification) {
            ext_idle_notification_v1_destroy(n->notification);
            n->notification = nullptr;
        }
    }
    m_notifications.clear();
}

void IdleApp::setup_signals() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGCHLD);

    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        std::cerr << "[miquidle] Warning: Failed to block signals: " << strerror(errno) << "\n";
    }

    m_signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (m_signal_fd < 0) {
        std::cerr << "[miquidle] Warning: Failed to create signalfd: " << strerror(errno) << "\n";
    }
}

void IdleApp::cleanup_signals() {
    if (m_signal_fd >= 0) {
        close(m_signal_fd);
        m_signal_fd = -1;
    }
}

void IdleApp::setup_inotify() {
    if (m_config_path.empty()) return;

    std::filesystem::path cfg(m_config_path);
    std::filesystem::path dir = cfg.parent_path();
    if (dir.empty()) dir = ".";

    m_inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_inotify_fd < 0) {
        std::cerr << "[miquidle] Warning: Failed to initialize inotify: " << strerror(errno) << "\n";
        return;
    }

    if (std::filesystem::exists(dir)) {
        m_inotify_dir_wd = inotify_add_watch(m_inotify_fd, dir.c_str(),
            IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE);
    }

    if (std::filesystem::exists(cfg)) {
        m_inotify_file_wd = inotify_add_watch(m_inotify_fd, cfg.c_str(),
            IN_MODIFY | IN_CLOSE_WRITE);
    }

    if (m_config.debug) {
        std::cout << "[miquidle] Watching configuration for live changes: " << m_config_path << "\n";
    }
}

void IdleApp::cleanup_inotify() {
    if (m_inotify_fd >= 0) {
        if (m_inotify_file_wd >= 0) inotify_rm_watch(m_inotify_fd, m_inotify_file_wd);
        if (m_inotify_dir_wd >= 0) inotify_rm_watch(m_inotify_fd, m_inotify_dir_wd);
        close(m_inotify_fd);
        m_inotify_fd = -1;
        m_inotify_file_wd = -1;
        m_inotify_dir_wd = -1;
    }
}

void IdleApp::handle_inotify() {
    char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t len;
    bool should_reload = false;
    std::string target_filename = std::filesystem::path(m_config_path).filename().string();

    while ((len = read(m_inotify_fd, buffer, sizeof(buffer))) > 0) {
        for (char* ptr = buffer; ptr < buffer + len; ) {
            auto* event = reinterpret_cast<const struct inotify_event*>(ptr);
            if (event->wd == m_inotify_file_wd) {
                should_reload = true;
            } else if (event->wd == m_inotify_dir_wd && event->len > 0) {
                if (event->name == target_filename) {
                    should_reload = true;
                }
            }
            ptr += sizeof(struct inotify_event) + event->len;
        }
    }

    if (should_reload) {
        if (std::filesystem::exists(m_config_path)) {
            if (m_inotify_file_wd >= 0) {
                inotify_rm_watch(m_inotify_fd, m_inotify_file_wd);
            }
            m_inotify_file_wd = inotify_add_watch(m_inotify_fd, m_config_path.c_str(),
                IN_MODIFY | IN_CLOSE_WRITE);
        }
        reload_config();
    }
}

void IdleApp::reload_config() {
    std::cout << "[miquidle] Live config change detected: reloading " << m_config_path << "...\n";

    Config new_config;
    new_config.debug = m_config.debug;
    new_config.wait = m_config.wait;
    new_config.seat_name = m_config.seat_name;

    if (!new_config.load_file(m_config_path)) {
        std::cerr << "[miquidle] Error parsing updated config; keeping previous configuration.\n";
        return;
    }

    clear_idle_listeners();
    m_config = std::move(new_config);
    setup_idle_listeners();
    setup_dbus();

    std::cout << "[miquidle] Configuration reloaded successfully (" 
              << m_config.listeners.size() << " listeners active)\n";
}

void IdleApp::setup_idle_listeners() {
    for (const auto& l : m_config.listeners) {
        if (l.timeout_sec == 0 || l.on_timeout.empty()) continue;

        auto active = std::make_unique<ActiveNotification>();
        active->listener_config = l;
        active->app = this;

        uint32_t timeout_ms = l.timeout_sec * 1000;
        active->notification = ext_idle_notifier_v1_get_idle_notification(
            m_idle_notifier,
            timeout_ms,
            m_seat
        );

        ext_idle_notification_v1_add_listener(
            active->notification,
            &idle_notification_listener,
            active.get()
        );

        if (m_config.debug) {
            std::cout << "[miquidle] Registered listener: timeout=" << l.timeout_sec 
                      << "s, on_timeout='" << l.on_timeout << "'";
            if (!l.on_resume.empty()) {
                std::cout << ", on_resume='" << l.on_resume << "'";
            }
            std::cout << "\n";
        }

        m_notifications.push_back(std::move(active));
    }

    wl_display_flush(m_display);
}

void IdleApp::setup_dbus() {
#ifdef HAVE_SYSTEMD
    m_dbus = std::make_unique<DBusManager>();
    if (m_dbus->init()) {
        if (!m_config.before_sleep_cmd.empty()) {
            m_dbus->set_before_sleep_handler([this]() {
                if (m_config.debug) {
                    std::cout << "[miquidle] PrepareForSleep: executing before_sleep_cmd: "
                              << m_config.before_sleep_cmd << "\n";
                }
                execute_command(m_config.before_sleep_cmd);
            });
        }

        if (!m_config.after_sleep_cmd.empty()) {
            m_dbus->set_after_sleep_handler([this]() {
                if (m_config.debug) {
                    std::cout << "[miquidle] Resume from sleep: executing after_sleep_cmd: "
                              << m_config.after_sleep_cmd << "\n";
                }
                execute_command(m_config.after_sleep_cmd);
            });
        }

        if (!m_config.lock_cmd.empty()) {
            m_dbus->set_lock_handler([this]() {
                if (m_config.debug) {
                    std::cout << "[miquidle] Session Lock: executing lock_cmd: " 
                              << m_config.lock_cmd << "\n";
                }
                execute_command(m_config.lock_cmd);
            });
        }

        if (!m_config.unlock_cmd.empty()) {
            m_dbus->set_unlock_handler([this]() {
                if (m_config.debug) {
                    std::cout << "[miquidle] Session Unlock: executing unlock_cmd: " 
                              << m_config.unlock_cmd << "\n";
                }
                execute_command(m_config.unlock_cmd);
            });
        }
    }
#endif
}

void IdleApp::execute_command(const std::string& cmd) {
    if (cmd.empty()) return;
    Process::execute(cmd, m_config.wait);
}

void IdleApp::trigger_immediate_idle() {
    if (m_config.debug) {
        std::cout << "[miquidle] SIGUSR1 received: triggering immediate idle actions\n";
    }

    for (auto& n : m_notifications) {
        if (!n->is_idled) {
            n->is_idled = true;
            if (m_config.debug) {
                std::cout << "[miquidle] Forcing idle for timeout " 
                          << n->listener_config.timeout_sec << "s: " 
                          << n->listener_config.on_timeout << "\n";
            }
            execute_command(n->listener_config.on_timeout);
        }
    }
}

void IdleApp::stop() {
    if (!m_running) return;
    m_running = false;

    // Run any pending resume commands on exit
    for (auto& n : m_notifications) {
        if (n->is_idled && !n->listener_config.on_resume.empty()) {
            if (m_config.debug) {
                std::cout << "[miquidle] Cleanup: executing pending resume: " 
                          << n->listener_config.on_resume << "\n";
            }
            execute_command(n->listener_config.on_resume);
            n->is_idled = false;
        }
    }
}

void IdleApp::run() {
    m_running = true;

    while (m_running) {
        while (wl_display_prepare_read(m_display) != 0) {
            wl_display_dispatch_pending(m_display);
        }
        wl_display_flush(m_display);

        struct pollfd pfd[4];
        int nfds = 2;

        pfd[0].fd = wl_display_get_fd(m_display);
        pfd[0].events = POLLIN;
        pfd[0].revents = 0;

        pfd[1].fd = m_signal_fd;
        pfd[1].events = POLLIN;
        pfd[1].revents = 0;

        int inotify_idx = -1;
        if (m_inotify_fd >= 0) {
            inotify_idx = nfds++;
            pfd[inotify_idx].fd = m_inotify_fd;
            pfd[inotify_idx].events = POLLIN;
            pfd[inotify_idx].revents = 0;
        }

        int dbus_idx = -1;
#ifdef HAVE_SYSTEMD
        if (m_dbus && m_dbus->get_fd() >= 0) {
            dbus_idx = nfds++;
            pfd[dbus_idx].fd = m_dbus->get_fd();
            pfd[dbus_idx].events = POLLIN;
            pfd[dbus_idx].revents = 0;
        }
#endif

        int ret = poll(pfd, nfds, -1);
        if (ret < 0) {
            if (errno == EINTR) {
                wl_display_cancel_read(m_display);
                continue;
            }
            wl_display_cancel_read(m_display);
            break;
        }

        if (pfd[0].revents & POLLIN) {
            wl_display_read_events(m_display);
        } else {
            wl_display_cancel_read(m_display);
        }

        wl_display_dispatch_pending(m_display);

        if (pfd[1].revents & POLLIN) {
            struct signalfd_siginfo fdsi;
            ssize_t s = read(m_signal_fd, &fdsi, sizeof(fdsi));
            if (s == sizeof(fdsi)) {
                if (fdsi.ssi_signo == SIGCHLD) {
                    Process::reap_children();
                } else if (fdsi.ssi_signo == SIGUSR1) {
                    trigger_immediate_idle();
                } else if (fdsi.ssi_signo == SIGINT || fdsi.ssi_signo == SIGTERM) {
                    stop();
                    break;
                }
            }
        }

        if (inotify_idx >= 0 && (pfd[inotify_idx].revents & POLLIN)) {
            handle_inotify();
        }

#ifdef HAVE_SYSTEMD
        if (dbus_idx >= 0 && (pfd[dbus_idx].revents & POLLIN)) {
            m_dbus->process();
        }
#endif
    }
}

void IdleApp::handle_global(void* data, struct wl_registry* registry, uint32_t name,
                           const char* interface, uint32_t version) {
    auto* app = static_cast<IdleApp*>(data);

    if (strcmp(interface, ext_idle_notifier_v1_interface.name) == 0) {
        app->m_idle_notifier = static_cast<struct ext_idle_notifier_v1*>(
            wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, 1)
        );
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        if (!app->m_seat) {
            app->m_seat = static_cast<struct wl_seat*>(
                wl_registry_bind(registry, name, &wl_seat_interface, 1)
            );
        }
    }
}

void IdleApp::handle_global_remove(void* data, struct wl_registry* registry, uint32_t name) {
    // No-op for runtime global removal
}

void IdleApp::handle_idled(void* data, struct ext_idle_notification_v1* ext_idle_notification_v1) {
    auto* notif = static_cast<ActiveNotification*>(data);
    if (!notif) return;

    notif->is_idled = true;
    if (notif->app->m_config.debug) {
        std::cout << "[miquidle] IDLED (timeout " << notif->listener_config.timeout_sec 
                  << "s reached): " << notif->listener_config.on_timeout << "\n";
    }

    notif->app->execute_command(notif->listener_config.on_timeout);
}

void IdleApp::handle_resumed(void* data, struct ext_idle_notification_v1* ext_idle_notification_v1) {
    auto* notif = static_cast<ActiveNotification*>(data);
    if (!notif) return;

    notif->is_idled = false;
    if (notif->app->m_config.debug) {
        std::cout << "[miquidle] RESUMED (activity detected for timeout " 
                  << notif->listener_config.timeout_sec << "s)";
        if (!notif->listener_config.on_resume.empty()) {
            std::cout << ": " << notif->listener_config.on_resume;
        }
        std::cout << "\n";
    }

    if (!notif->listener_config.on_resume.empty()) {
        notif->app->execute_command(notif->listener_config.on_resume);
    }
}

} // namespace miquidle
