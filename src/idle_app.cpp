#include "idle_app.hpp"
#include "process.hpp"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/inotify.h>
#include <csignal>
#include <filesystem>
#include <poll.h>

namespace miquidle {

static IdleApp* s_current_app = nullptr;

static void signal_handler(int sig) {
    if (!s_current_app) return;
    if (sig == SIGINT || sig == SIGTERM) {
        s_current_app->stop();
    } else if (sig == SIGUSR1) {
        s_current_app->trigger_immediate_idle();
    }
}

IdleApp::IdleApp(Config config, std::string config_path)
    : m_config(std::move(config)), m_config_path(Config::expand_home(std::move(config_path))) {
    s_current_app = this;
}

IdleApp::~IdleApp() {
    stop();
    cleanup_inotify();
    if (s_current_app == this) {
        s_current_app = nullptr;
    }
}

bool IdleApp::init() {
    m_engine = miqu::AppEngine::create();
    if (!m_engine) {
        std::cerr << "[miquidle] Error: Cannot connect to Wayland display. Is a compositor running?\n";
        return false;
    }

    m_engine->set_quit_on_last_window_closed(false);
    miqu::IdleManager::get()->init(m_engine.get());

    if (!miqu::IdleManager::get()->is_supported()) {
        std::cerr << "[miquidle] Error: Compositor does not support 'ext_idle_notifier_v1'.\n";
        return false;
    }

    m_running = true;
    setup_signals();
    setup_inotify();
    setup_idle_listeners();
    setup_dbus();

    return true;
}

void IdleApp::clear_idle_listeners() {
    miqu::IdleManager::get()->clear_listeners();
    if (m_engine && m_engine->get_display()) {
        wl_display_flush(m_engine->get_display());
    }
}

void IdleApp::setup_signals() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGUSR1, signal_handler);
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

    m_inotify_thread = std::thread([this, target_filename = cfg.filename().string()]() {
        char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
        struct pollfd pfd;
        pfd.fd = m_inotify_fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        while (m_running) {
            int ret = poll(&pfd, 1, 500);
            if (ret <= 0) {
                continue;
            }

            if (!(pfd.revents & POLLIN)) {
                continue;
            }

            ssize_t len = read(m_inotify_fd, buffer, sizeof(buffer));
            if (len <= 0) {
                continue;
            }

            bool should_reload = false;
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

            if (should_reload) {
                // Debounce slightly to ensure writer has closed file
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // Re-watch file in case of atomic replacement (e.g. rename by editors)
                if (std::filesystem::exists(m_config_path)) {
                    if (m_inotify_file_wd >= 0) {
                        inotify_rm_watch(m_inotify_fd, m_inotify_file_wd);
                    }
                    m_inotify_file_wd = inotify_add_watch(m_inotify_fd, m_config_path.c_str(),
                        IN_MODIFY | IN_CLOSE_WRITE);
                }

                // Drain any additional events queued during write
                while (true) {
                    struct pollfd drain_pfd{m_inotify_fd, POLLIN, 0};
                    if (poll(&drain_pfd, 1, 30) > 0 && (drain_pfd.revents & POLLIN)) {
                        (void)read(m_inotify_fd, buffer, sizeof(buffer));
                    } else {
                        break;
                    }
                }

                if (m_engine) {
                    m_engine->post([this]() {
                        reload_config();
                    });
                }
            }
        }
    });
}

void IdleApp::cleanup_inotify() {
    m_running = false;
    if (m_inotify_fd >= 0) {
        int fd = m_inotify_fd;
        m_inotify_fd = -1;
        if (m_inotify_file_wd >= 0) {
            inotify_rm_watch(fd, m_inotify_file_wd);
            m_inotify_file_wd = -1;
        }
        if (m_inotify_dir_wd >= 0) {
            inotify_rm_watch(fd, m_inotify_dir_wd);
            m_inotify_dir_wd = -1;
        }
        close(fd);
    }
    if (m_inotify_thread.joinable()) {
        m_inotify_thread.join();
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

    if (m_engine && m_engine->get_display()) {
        wl_display_flush(m_engine->get_display());
    }

    std::cout << "[miquidle] Configuration reloaded successfully (" 
              << m_config.listeners.size() << " listeners active)\n";
}

void IdleApp::setup_idle_listeners() {
    for (const auto& l : m_config.listeners) {
        if (l.timeout_sec == 0 || l.on_timeout.empty()) continue;

        miqu::IdleManager::get()->add_listener(
            l.timeout_sec,
            [this, l]() {
                if (m_config.debug) {
                    std::cout << "[miquidle] Timeout " << l.timeout_sec 
                              << "s reached. Executing: " << l.on_timeout << "\n";
                }
                execute_command(l.on_timeout);
            },
            [this, l]() {
                if (!l.on_resume.empty()) {
                    if (m_config.debug) {
                        std::cout << "[miquidle] Resumed from " << l.timeout_sec 
                                  << "s idle. Executing: " << l.on_resume << "\n";
                    }
                    execute_command(l.on_resume);
                }
            }
        );

        if (m_config.debug) {
            std::cout << "[miquidle] Registered listener via IdleManager: timeout=" << l.timeout_sec 
                      << "s, on_timeout='" << l.on_timeout << "'";
            if (!l.on_resume.empty()) {
                std::cout << ", on_resume='" << l.on_resume << "'";
            }
            std::cout << "\n";
        }
    }
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

    for (const auto& l : m_config.listeners) {
        if (!l.on_timeout.empty()) {
            execute_command(l.on_timeout);
        }
    }
}

void IdleApp::stop() {
    if (!m_running) return;
    m_running = false;

    clear_idle_listeners();
    if (m_engine) {
        m_engine->quit(0);
    }
}

void IdleApp::run() {
    if (!m_engine) return;
    m_running = true;
    m_engine->enter_loop();
}

} // namespace miquidle
