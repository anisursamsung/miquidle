#pragma once

#include <string>
#include <sys/types.h>

namespace miquidle {

class Process {
public:
    // Execute command with /bin/sh -c. If wait is true, blocks until command finishes.
    static pid_t execute(const std::string& command, bool wait = false);

    // Reaps any finished child processes without blocking
    static void reap_children();
};

} // namespace miquidle
