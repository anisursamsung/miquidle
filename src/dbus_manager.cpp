#ifdef HAVE_SYSTEMD
#include "dbus_manager.hpp"

#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <chrono>
#include <thread>

namespace miquidle {

DBusManager::DBusManager() = default;

DBusManager::~DBusManager() {
    stop();
}

bool DBusManager::init() {
    int r = sd_bus_open_system(&m_bus);
    if (r < 0) {
        std::cerr << "[miquidle] Warning: Failed to connect to system bus: " << strerror(-r) << "\n";
        return false;
    }

    // Match PrepareForSleep
    r = sd_bus_match_signal(
        m_bus,
        &m_sleep_slot,
        "org.freedesktop.login1",
        "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager",
        "PrepareForSleep",
        handle_prepare_for_sleep,
        this
    );
    if (r < 0) {
        std::cerr << "[miquidle] Warning: Failed to subscribe to PrepareForSleep: " << strerror(-r) << "\n";
    }

    // Match Session Lock & Unlock
    r = sd_bus_match_signal(
        m_bus,
        &m_lock_slot,
        "org.freedesktop.login1",
        nullptr,
        "org.freedesktop.login1.Session",
        "Lock",
        handle_session_lock,
        this
    );
    if (r < 0) {
        std::cerr << "[miquidle] Warning: Failed to subscribe to Session Lock: " << strerror(-r) << "\n";
    }

    r = sd_bus_match_signal(
        m_bus,
        &m_unlock_slot,
        "org.freedesktop.login1",
        nullptr,
        "org.freedesktop.login1.Session",
        "Unlock",
        handle_session_unlock,
        this
    );
    if (r < 0) {
        std::cerr << "[miquidle] Warning: Failed to subscribe to Session Unlock: " << strerror(-r) << "\n";
    }

    take_sleep_inhibitor();

    m_running = true;
    m_thread = std::thread(&DBusManager::run_worker, this);

    return true;
}

void DBusManager::stop() {
    if (!m_running) return;
    m_running = false;

    if (m_bus) {
        sd_bus_close(m_bus);
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    release_sleep_inhibitor();

    if (m_sleep_slot) {
        sd_bus_slot_unref(m_sleep_slot);
        m_sleep_slot = nullptr;
    }
    if (m_lock_slot) {
        sd_bus_slot_unref(m_lock_slot);
        m_lock_slot = nullptr;
    }
    if (m_unlock_slot) {
        sd_bus_slot_unref(m_unlock_slot);
        m_unlock_slot = nullptr;
    }

    if (m_bus) {
        sd_bus_flush_close_unref(m_bus);
        m_bus = nullptr;
    }
}

void DBusManager::run_worker() {
    while (m_running) {
        int r = sd_bus_process(m_bus, nullptr);
        if (r < 0) {
            if (m_running) {
                std::cerr << "[miquidle] D-Bus process error: " << strerror(-r) << "\n";
            }
            break;
        }
        if (r > 0) {
            // Further work queued to be dispatched immediately
            continue;
        }

        // Wait with 250ms timeout to periodically check m_running
        r = sd_bus_wait(m_bus, 250000);
        if (r < 0 && -r != EINTR) {
            if (m_running) {
                std::cerr << "[miquidle] D-Bus wait error: " << strerror(-r) << "\n";
            }
            break;
        }
    }
}

int DBusManager::get_fd() const {
    if (!m_bus) return -1;
    return sd_bus_get_fd(m_bus);
}

void DBusManager::process() {
    if (!m_bus) return;
    int r = 0;
    while ((r = sd_bus_process(m_bus, nullptr)) > 0) {
        // process all queued messages
    }
}

void DBusManager::take_sleep_inhibitor() {
    if (!m_bus || m_inhibitor_fd >= 0) return;

    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    int r = sd_bus_call_method(
        m_bus,
        "org.freedesktop.login1",
        "/org/freedesktop/login1",
        "org.freedesktop.login1.Manager",
        "Inhibit",
        &error,
        &reply,
        "ssss",
        "sleep",
        "miquidle",
        "Lock session / prepare before sleep",
        "delay"
    );

    if (r >= 0) {
        int fd = -1;
        r = sd_bus_message_read(reply, "h", &fd);
        if (r >= 0 && fd >= 0) {
            m_inhibitor_fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
        }
    } else {
        std::cerr << "[miquidle] Note: Could not acquire sleep inhibitor: " 
                  << (error.message ? error.message : strerror(-r)) << "\n";
    }

    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
}

void DBusManager::release_sleep_inhibitor() {
    if (m_inhibitor_fd >= 0) {
        close(m_inhibitor_fd);
        m_inhibitor_fd = -1;
    }
}

int DBusManager::handle_prepare_for_sleep(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
    auto* self = static_cast<DBusManager*>(userdata);
    int going_to_sleep = 0;
    int r = sd_bus_message_read(m, "b", &going_to_sleep);
    if (r < 0) return r;

    if (going_to_sleep) {
        if (self->m_before_sleep_cb) {
            self->m_before_sleep_cb();
        }
        // Give the lock application time to map and acquire the session lock
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        // Release inhibitor so sleep can proceed immediately
        self->release_sleep_inhibitor();
    } else {
        // Resuming from sleep
        if (self->m_after_sleep_cb) {
            self->m_after_sleep_cb();
        }
        // Re-take inhibitor for the next sleep
        self->take_sleep_inhibitor();
    }

    return 0;
}

int DBusManager::handle_session_lock(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
    auto* self = static_cast<DBusManager*>(userdata);
    if (self && self->m_lock_cb) {
        self->m_lock_cb();
    }
    return 0;
}

int DBusManager::handle_session_unlock(sd_bus_message* m, void* userdata, sd_bus_error* ret_error) {
    auto* self = static_cast<DBusManager*>(userdata);
    if (self && self->m_unlock_cb) {
        self->m_unlock_cb();
    }
    return 0;
}

} // namespace miquidle
#endif
