#pragma once

#include "config.hpp"
#include <miqutoolkit/miqutoolkit.hpp>

#ifdef HAVE_SYSTEMD
#include "dbus_manager.hpp"
#endif

#include <memory>
#include <vector>
#include <thread>
#include <atomic>

namespace miquidle {

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
    std::atomic<bool> m_running{false};

    std::shared_ptr<miqu::AppEngine> m_engine;

#ifdef HAVE_SYSTEMD
    std::unique_ptr<DBusManager> m_dbus;
#endif

    int m_inotify_fd = -1;
    int m_inotify_dir_wd = -1;
    int m_inotify_file_wd = -1;
    std::thread m_inotify_thread;

    void setup_signals();
    void setup_inotify();
    void cleanup_inotify();
    void setup_idle_listeners();
    void clear_idle_listeners();
    void setup_dbus();

    void execute_command(const std::string& cmd);
};

} // namespace miquidle
