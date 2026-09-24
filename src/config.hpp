#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace miquidle {

struct Listener {
    uint32_t timeout_sec = 0;
    std::string on_timeout;
    std::string on_resume;
};

struct Config {
    std::string lock_cmd;
    std::string unlock_cmd;
    std::string before_sleep_cmd;
    std::string after_sleep_cmd;

    bool ignore_sleep_inhibit = false;
    bool wait = false;
    bool debug = false;
    std::string seat_name;

    std::vector<Listener> listeners;

    // Attempts to load configuration from given path
    bool load_file(const std::string& path);

    // Returns the standard user configuration path in ~/.config/miquidle/miquidle.conf
    static std::string get_user_config_path();

    // Ensures user config exists, copying default on first launch if needed
    static std::string init_user_config();

    // Returns first existing configuration file path, ensuring user config on first launch
    static std::string find_default_config();

    // Expands leading ~ in path
    static std::string expand_home(const std::string& path);
};

} // namespace miquidle
