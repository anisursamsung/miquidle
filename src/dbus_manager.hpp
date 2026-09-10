#pragma once

#ifdef HAVE_SYSTEMD
#include <systemd/sd-bus.h>
#include <functional>
#include <string>

namespace miquidle {

class DBusManager {
public:
    using Callback = std::function<void()>;

    DBusManager();
    ~DBusManager();

    bool init();
    int get_fd() const;
    void process();

    void set_before_sleep_handler(Callback cb) { m_before_sleep_cb = cb; }
    void set_after_sleep_handler(Callback cb) { m_after_sleep_cb = cb; }
    void set_lock_handler(Callback cb) { m_lock_cb = cb; }
    void set_unlock_handler(Callback cb) { m_unlock_cb = cb; }

    void take_sleep_inhibitor();
    void release_sleep_inhibitor();

private:
    sd_bus* m_bus = nullptr;
    sd_bus_slot* m_sleep_slot = nullptr;
    sd_bus_slot* m_lock_slot = nullptr;
    sd_bus_slot* m_unlock_slot = nullptr;
    int m_inhibitor_fd = -1;

    Callback m_before_sleep_cb;
    Callback m_after_sleep_cb;
    Callback m_lock_cb;
    Callback m_unlock_cb;

    static int handle_prepare_for_sleep(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static int handle_session_lock(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
    static int handle_session_unlock(sd_bus_message* m, void* userdata, sd_bus_error* ret_error);
};

} // namespace miquidle
#endif
