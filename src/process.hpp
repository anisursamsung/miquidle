#pragma once

#include <string>
#include <sys/types.h>

namespace miquidle {

class Process {
public:
    // Execute command with /bin/sh -c. If wait is true, blocks until command finishes.
    static pid_t execute(const std::string& command, bool wait = false);

    // Execute command synchronously with timeout in milliseconds using pidfd_open/poll
    static int execute_sync(const std::string& command, int timeout_ms = 4000);

    // Reaps any finished child processes without blocking
    static void reap_children();
};

} // namespace miquidle
