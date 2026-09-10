#pragma once

#include "config.hpp"
#include <wayland-client.h>
#include "ext-idle-notify-v1-client-protocol.h"

#ifdef HAVE_SYSTEMD
#include "dbus_manager.hpp"
#endif

#include <memory>
#include <vector>

namespace miquidle {

struct ActiveNotification {
    struct ext_idle_notification_v1* notification = nullptr;
    Listener listener_config;
    bool is_idled = false;
    class IdleApp* app = nullptr;
};

class IdleApp {
public:
    explicit IdleApp(Config config, std::string config_path = "");
    ~IdleApp();

    bool init();
    void run();
    void stop();

    void trigger_immediate_idle();
    void reload_config();

private:
    Config m_config;
    std::string m_config_path;
    bool m_running = false;

    struct wl_display* m_display = nullptr;
    struct wl_registry* m_registry = nullptr;
    struct wl_seat* m_seat = nullptr;
    struct ext_idle_notifier_v1* m_idle_notifier = nullptr;

    std::vector<std::unique_ptr<ActiveNotification>> m_notifications;

#ifdef HAVE_SYSTEMD
    std::unique_ptr<DBusManager> m_dbus;
#endif

    int m_signal_fd = -1;
    int m_inotify_fd = -1;
    int m_inotify_dir_wd = -1;
    int m_inotify_file_wd = -1;

    void setup_signals();
    void cleanup_signals();
    void setup_inotify();
    void cleanup_inotify();
    void handle_inotify();
    void setup_idle_listeners();
    void clear_idle_listeners();
    void setup_dbus();

    void execute_command(const std::string& cmd);

public:
    // Wayland Registry callbacks
    static void handle_global(void* data, struct wl_registry* registry, uint32_t name,
                             const char* interface, uint32_t version);
    static void handle_global_remove(void* data, struct wl_registry* registry, uint32_t name);

    // Idle Notification callbacks
    static void handle_idled(void* data, struct ext_idle_notification_v1* ext_idle_notification_v1);
    static void handle_resumed(void* data, struct ext_idle_notification_v1* ext_idle_notification_v1);
};

} // namespace miquidle
