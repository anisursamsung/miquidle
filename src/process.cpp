#include "process.hpp"

#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

namespace miquidle {

pid_t Process::execute(const std::string& command, bool wait) {
    if (command.empty()) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[miquidle] Failed to fork process for command: " << command << "\n";
        return -1;
    }

    if (pid == 0) {
        // In child: reset signals and run via shell
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        execl("/bin/sh", "sh", "-c", command.c_str(), (char*)nullptr);
        _exit(127);
    }

    // In parent
    if (wait) {
        int status = 0;
        waitpid(pid, &status, 0);
        return pid;
    }

    return pid;
}

void Process::reap_children() {
    int status = 0;
    while (waitpid(-1, &status, WNOHANG) > 0) {
        // reap all available zombies
    }
}

} // namespace miquidle
