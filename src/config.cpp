#include "config.hpp"
#include <miqutoolkit/core/config.hpp>

#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cstdlib>

namespace miquidle {

namespace fs = std::filesystem;

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n\"'");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n\"'");
    return str.substr(first, (last - first + 1));
}

std::string Config::expand_home(const std::string& path) {
    if (path.empty()) return path;
    if (path[0] == '~') {
        const char* home = getenv("HOME");
        if (home) {
            return std::string(home) + path.substr(1);
        }
    }
    return path;
}

static bool parse_bool(const std::string& val) {
    std::string s = val;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return (s == "true" || s == "1" || s == "yes" || s == "on");
}

std::string Config::get_user_config_path() {
    const char* xdg_config = getenv("XDG_CONFIG_HOME");
    if (xdg_config && *xdg_config) {
        return std::string(xdg_config) + "/miquidle/miquidle.conf";
    }
    const char* home = getenv("HOME");
    if (home && *home) {
        return std::string(home) + "/.config/miquidle/miquidle.conf";
    }
    return "";
}

std::string Config::init_user_config() {
    return miqu::Config::init_user_config("miquidle", "miquidle.conf");
}

std::string Config::find_default_config() {
    // 1. Check user configuration (without auto-seeding)
    std::string user_conf = get_user_config_path();
    if (!user_conf.empty() && fs::exists(user_conf)) {
        return user_conf;
    }

    // 2. Check alternative user config naming (e.g. ~/.config/miquidle/config)
    const char* xdg_config = getenv("XDG_CONFIG_HOME");
    const char* home = getenv("HOME");
    if (xdg_config && *xdg_config) {
        std::string alt = std::string(xdg_config) + "/miquidle/config";
        if (fs::exists(alt)) return alt;
    } else if (home && *home) {
        std::string alt = std::string(home) + "/.config/miquidle/config";
        if (fs::exists(alt)) return alt;
    }

    return "";
}

bool Config::load_file(const std::string& path) {
    std::string full_path = expand_home(path);
    std::ifstream file(full_path);
    if (!file.is_open()) {
        std::cerr << "[miquidle] Unable to open config file: " << full_path << "\n";
        return false;
    }

    std::string line;
    bool in_listener = false;
    Listener current_listener;

    auto finish_listener = [&]() {
        if (in_listener) {
            if (current_listener.timeout_sec > 0 && !current_listener.on_timeout.empty()) {
                listeners.push_back(current_listener);
            }
            current_listener = Listener{};
            in_listener = false;
        }
    };

    while (std::getline(file, line)) {
        // Strip comments
        auto hash_pos = line.find('#');
        if (hash_pos != std::string::npos) {
            line = line.substr(0, hash_pos);
        }
        line = trim(line);
        if (line.empty()) continue;

        // Check for block starts / ends
        if (line == "listener {" || line == "listener" || line == "[listener]" || line == "[[listener]]") {
            finish_listener();
            in_listener = true;
            continue;
        }
        if (line == "}") {
            finish_listener();
            continue;
        }
        if (line == "[general]") {
            finish_listener();
            continue;
        }

        // Check for swayidle-style line e.g. "timeout 300 'cmd' resume 'cmd'"
        if (line.rfind("timeout ", 0) == 0 && line.find('=') == std::string::npos) {
            finish_listener();
            std::istringstream iss(line);
            std::string kw, cmd, resume_kw, resume_cmd;
            uint32_t t = 0;
            iss >> kw >> t;
            // rest of string
            std::string remaining;
            std::getline(iss, remaining);
            remaining = trim(remaining);

            auto r_pos = remaining.find("resume ");
            if (r_pos != std::string::npos) {
                cmd = trim(remaining.substr(0, r_pos));
                resume_cmd = trim(remaining.substr(r_pos + 7));
            } else {
                cmd = remaining;
            }

            if (t > 0 && !cmd.empty()) {
                Listener l;
                l.timeout_sec = t;
                l.on_timeout = cmd;
                l.on_resume = resume_cmd;
                listeners.push_back(l);
            }
            continue;
        }

        // Key = Value
        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        std::string key = trim(line.substr(0, eq_pos));
        std::string val = trim(line.substr(eq_pos + 1));
        std::transform(key.begin(), key.end(), key.begin(), ::tolower);

        if (in_listener) {
            if (key == "timeout") {
                try {
                    current_listener.timeout_sec = std::stoul(val);
                } catch (...) {}
            } else if (key == "on-timeout" || key == "on_timeout" || key == "command" || key == "cmd") {
                current_listener.on_timeout = val;
            } else if (key == "on-resume" || key == "on_resume" || key == "resume") {
                current_listener.on_resume = val;
            }
        } else {
            // General settings
            if (key == "lock_cmd" || key == "lock-cmd" || key == "lock") {
                lock_cmd = val;
            } else if (key == "unlock_cmd" || key == "unlock-cmd" || key == "unlock") {
                unlock_cmd = val;
            } else if (key == "before_sleep_cmd" || key == "before-sleep-cmd" || key == "before_sleep") {
                before_sleep_cmd = val;
            } else if (key == "after_sleep_cmd" || key == "after-sleep-cmd" || key == "after_sleep") {
                after_sleep_cmd = val;
            } else if (key == "ignore_sleep_inhibit" || key == "ignore-sleep-inhibit") {
                ignore_sleep_inhibit = parse_bool(val);
            } else if (key == "wait") {
                wait = parse_bool(val);
            } else if (key == "seat") {
                seat_name = val;
            }
        }
    }

    finish_listener();
    return true;
}

} // namespace miquidle
