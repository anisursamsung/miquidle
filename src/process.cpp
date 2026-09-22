#include "process.hpp"

#include <iostream>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <poll.h>
#include <signal.h>
#include <chrono>
#include <ctime>
#include <cerrno>
#include <cstring>

namespace miquidle {

pid_t Process::execute(const std::string& command, bool wait) {
    if (command.empty()) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[miquidle] Failed to fork process for command: " << command << "\n";
        return -1;
    }

    if (pid == 0) {
        if (!wait) {
            // Double-fork so grandchild is reparented to init and will not leak as zombie
            if (fork() != 0) {
                _exit(0);
            }
            setsid();
        }

        // In child/grandchild: reset signals and run via shell
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
    } else {
        // Collect intermediate child immediately
        waitpid(pid, nullptr, 0);
        return 0;
    }
}

int Process::execute_sync(const std::string& command, int timeout_ms) {
    if (command.empty()) return -1;

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "[miquidle] Failed to fork process for command: " << command << "\n";
        return -1;
    }

    if (pid == 0) {
        // Reset signals and run via shell
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGCHLD, SIG_DFL);

        execl("/bin/sh", "sh", "-c", command.c_str(), (char*)nullptr);
        _exit(127);
    }

    // In parent: wait for child with event-driven timeout via pidfd_open/poll
    int status = 0;
#ifdef SYS_pidfd_open
    int pfd = syscall(SYS_pidfd_open, pid, 0);
    if (pfd >= 0) {
        struct pollfd poll_item = { pfd, POLLIN, 0 };
        int ret = poll(&poll_item, 1, timeout_ms > 0 ? timeout_ms : -1);
        close(pfd);

        if (ret > 0) {
            waitpid(pid, &status, 0);
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        } else if (ret == 0) {
            std::cerr << "[miquidle] Warning: Command timed out after " << timeout_ms << "ms: " << command << "\n";
            kill(pid, SIGTERM);
            struct timespec req = { 0, 50000000 }; // 50ms
            nanosleep(&req, nullptr);
            if (waitpid(pid, &status, WNOHANG) <= 0) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
            }
            return -1;
        } else {
            waitpid(pid, &status, 0);
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        }
    }
#endif

    // Fallback if pidfd_open is unavailable
    auto start = std::chrono::steady_clock::now();
    while (true) {
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) {
            return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        } else if (ret < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (timeout_ms > 0 && elapsed >= timeout_ms) {
            std::cerr << "[miquidle] Warning: Command timed out after " << timeout_ms << "ms: " << command << "\n";
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return -1;
        }

        struct timespec ts = { 0, 10000000 }; // 10ms
        nanosleep(&ts, nullptr);
    }
}

void Process::reap_children() {
    int status = 0;
    while (waitpid(-1, &status, WNOHANG) > 0) {
        // reap any remaining zombies
    }
}

} // namespace miquidle
