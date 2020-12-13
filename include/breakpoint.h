#include <sys/wait.h>
#include <sys/ptrace.h>
#include <unistd.h>
#include <bits/stdc++.h>

class breakpoint {

    public:
        breakpoint() = default;
        breakpoint(pid_t pid, intptr_t addr) : m_pid{pid}, m_addr{addr}, m_enabled{false}, m_data{NULL} {}

        void enable();
        void disable();

        bool is_enabled() {
            return m_enabled;
        }
        intptr_t get_addr() {
            return m_addr;
        }

    private:
        pid_t m_pid;
        intptr_t m_addr;
        uint8_t m_data;
        bool m_enabled;

};

// Adding 0xcc to the data of particular process and address defines the unique behaviour of breakpoint
void breakpoint::enable() {

    // Get data using ptrace using process ID and address
    auto data = ptrace(PTRACE_PEEKDATA, m_pid, m_addr, nullptr);

    // Saving the bottom bytes of data
    m_data = static_cast<uint8_t>(data & 0xff);
    
    // Set bottom bytes to 0xcc
    uint64_t updated_data = ((data & ~0xff) | 0xcc);

    // Updating data at the address
    ptrace(PTRACE_POKEDATA, m_pid, m_addr, updated_data);

    m_enabled = true;

}

void breakpoint::disable() {

    // Get data using ptrace using process ID and address
    auto data = ptrace(PTRACE_PEEKDATA, m_pid, m_addr, nullptr);

    // Restoring back the data by attaching the removed bottom bytes
    auto restored_data = ((data & ~0xff) | m_data);

    // Updating data at the address
    ptrace(PTRACE_POKEDATA, m_pid, m_addr, restored_data);
    m_enabled = false;

}
