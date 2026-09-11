#include "config.hpp"
#include "idle_app.hpp"

#include <iostream>
#include <string>
#include <vector>

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS] [EVENTS...]\n\n"
              << "A modern Wayland idle daemon for the Miquland desktop.\n\n"
              << "Options:\n"
              << "  -h, --help            Show this help message and exit\n"
              << "  -c, -C, --config PATH Path to configuration file\n"
              << "  -d, --debug           Enable verbose debug output\n"
              << "  -w, --wait            Wait for commands to finish executing\n"
              << "  -S, --seat SEAT       Specify Wayland seat name\n\n"
              << "Events (swayidle compatibility):\n"
              << "  timeout <seconds> <cmd> [resume <resume_cmd>]\n"
              << "  before-sleep <cmd>\n"
              << "  after-resume <cmd>\n"
              << "  lock <cmd>\n"
              << "  unlock <cmd>\n\n"
              << "Examples:\n"
              << "  " << prog << "\n"
              << "  " << prog << " -c ~/.config/miquidle/miquidle.conf\n"
              << "  " << prog << " timeout 300 'miqulock' timeout 600 'wlr-randr --output eDP-1 --off' resume 'wlr-randr --output eDP-1 --on'\n";
}

int main(int argc, char** argv) {
    miquidle::Config config;
    std::string config_path;

    // First pass: extract options
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-d" || arg == "--debug") {
            config.debug = true;
        } else if (arg == "-w" || arg == "--wait") {
            config.wait = true;
        } else if ((arg == "-c" || arg == "-C" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if ((arg == "-S" || arg == "--seat") && i + 1 < argc) {
            config.seat_name = argv[++i];
        } else {
            args.push_back(arg);
        }
    }

    // Determine config file to load
    if (config_path.empty()) {
        config_path = miquidle::Config::find_default_config();
    } else {
        config_path = miquidle::Config::expand_home(config_path);
    }

    if (!config_path.empty()) {
        if (config.debug) {
            std::cout << "[miquidle] Loading configuration from: " << config_path << "\n";
        }
        config.load_file(config_path);
    }

    // Parse remaining positional event arguments
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& ev = args[i];
        if (ev == "timeout" && i + 2 < args.size()) {
            uint32_t t = 0;
            try {
                t = std::stoul(args[++i]);
            } catch (...) {
                continue;
            }
            std::string cmd = args[++i];
            std::string resume_cmd;
            if (i + 2 < args.size() && args[i + 1] == "resume") {
                i += 2;
                resume_cmd = args[i];
            }
            config.listeners.push_back({t, cmd, resume_cmd});
        } else if (ev == "before-sleep" && i + 1 < args.size()) {
            config.before_sleep_cmd = args[++i];
        } else if (ev == "after-resume" && i + 1 < args.size()) {
            config.after_sleep_cmd = args[++i];
        } else if (ev == "lock" && i + 1 < args.size()) {
            config.lock_cmd = args[++i];
        } else if (ev == "unlock" && i + 1 < args.size()) {
            config.unlock_cmd = args[++i];
        }
    }

    if (config.listeners.empty() && config.before_sleep_cmd.empty() && config.lock_cmd.empty()) {
        std::cerr << "[miquidle] Notice: No idle listeners or sleep/lock hooks configured.\n"
                  << "Run '" << argv[0] << " -h' for usage information.\n";
    }

    miquidle::IdleApp app(std::move(config), config_path);
    if (!app.init()) {
        return 1;
    }

    app.run();
    return 0;
}
