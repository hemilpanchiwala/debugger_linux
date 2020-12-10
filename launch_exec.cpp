#include <iostream>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/personality.h>
#include <fcntl.h>
#include <unistd.h>
#include <bits/stdc++.h>

#include "linenoise.hpp"
#include "helper.h"
#include "breakpoint.h"
#include "registers.h"
#include "dwarf/dwarf++.hh"
#include "elf/elf++.hh"

using namespace std;

class debugger {

    public:
        debugger(string prog_name, pid_t pid) : m_prog_name{move(prog_name)}, m_pid{pid} {

            auto file = open(m_prog_name.c_str(), O_RDONLY);

            m_elf = elf::elf{
                elf::create_mmap_loader(file)
            };

            m_dwarf = dwarf::dwarf{
                dwarf::elf::create_loader(m_elf)
            };

        }

        void run();
        void runCommand(const string& line);
        void continue_execution();
        void addBreakpoint(intptr_t addr);
        void dump_registers();
        uint64_t get_program_counter();
        void set_program_counter(uint64_t pc);
        void step_over_breakpoint();
        dwarf::die get_func_using_pc(uint64_t pc);
        dwarf::line_table::iterator get_line_entry_using_pc(uint64_t pc);
        void initialize_load_address();
        uint64_t get_offset_load_address(uint64_t addr);

    private:
        string m_prog_name;
        pid_t m_pid;
        unordered_map<intptr_t, breakpoint> addr_to_bp;
        dwarf::dwarf m_dwarf;
        elf::elf m_elf;
        uint64_t m_load_address;

};


void debugger::run() {

    int wait_status;

    // wait for process to change state
    waitpid(m_pid, &wait_status, 0);

    string line = "";
    
    while(true) {
        auto quit = linenoise::Readline("hello> ", line);

        if(quit) break;

        runCommand(line);
        linenoise::AddHistory(line.c_str());
    }

}

void debugger::runCommand(const string& line) {

    auto args = split(line, ' ');
    auto input_command = args[0];

    if (is_prefix(input_command, "continue")) {
        continue_execution();
    } else if (is_prefix(input_command, "break")) {
        // Removing first 2 char from address as it contains 0x
        string addr {args[1], 2};

        // Taking first 16 bytes from the address
        auto m_addr = stol(addr, 0, 16);
        addBreakpoint(m_addr);
    } else if (is_prefix(input_command, "register")) {
        if (is_prefix(args[1], "dump")) {
            dump_registers();
        } else if (is_prefix(args[1], "read")) {
            cout<<get_register_value_from_type(m_pid, get_register_type_from_name(args[2]))<<endl;
        } else if (is_prefix(args[1], "write")) {
            string val {args[3], 2};
            set_register_value(m_pid, get_register_type_from_name(args[2]), stol(val, 0, 16));
        }
    } else if (is_prefix(input_command, "memory")) {
        string addr {args[2], 2};

        if(is_prefix(args[1], "read")) {
            cout<<"READ: "<<ptrace(PTRACE_PEEKDATA, m_pid, stol(addr, 0, 16), nullptr)<<endl;
        } else if(is_prefix(args[1], "write")) {
            string value {args[3], 2};
            ptrace(PTRACE_POKEDATA, m_pid, stol(addr, 0, 16), stol(value, 0, 16));
        }

    } else {
        cerr<<"No command found!! \n";
    }

}

void debugger::continue_execution() {

    step_over_breakpoint();
    ptrace(PTRACE_CONT, m_pid, nullptr, nullptr);

    int wait_status;
    // wait for process to change state
    waitpid(m_pid, &wait_status, 0);

}

void debugger::addBreakpoint(intptr_t addr) {

    cout<<"Set breakpoint at address 0x"<<hex<<addr<<endl;
    breakpoint bp{m_pid, addr};
    bp.enable();
    addr_to_bp[addr] = bp;

}

void debugger::dump_registers() {

    for (const auto& rg: registers) {
        cout<<"Register "<<rg.name<<" "<<get_register_value_from_type(m_pid, rg.r_type)<<endl;
    }

}

uint64_t debugger::get_program_counter() {
    return get_register_value_from_type(m_pid, register_type::rip);
}

void debugger::set_program_counter(uint64_t pc) {
    set_register_value(m_pid, register_type::rip, pc);
}

void debugger::step_over_breakpoint() {
    auto loc = get_program_counter() - 1;

    if(addr_to_bp.count(loc)) {
        auto& breakpoint = addr_to_bp[loc];

        if (breakpoint.is_enabled()) {
            auto prev_loc = loc;
            set_program_counter(prev_loc);

            breakpoint.disable();
            ptrace(PTRACE_SINGLESTEP, m_pid, 0, nullptr);
            int wait_status;
            waitpid(m_pid, &wait_status, 0);
            breakpoint.enable();
        }
    }
}

void debugger::initialize_load_address() {

    // This checks whether the file is dynamic library
    if(m_elf.get_hdr().type == elf::et::dyn) {   
        // Load address is present at /proc/process_pid/maps file
        ifstream map("/proc/" + to_string(m_pid) + "/maps");

        // The first in the file is the load address
        string address;
        getline(map, address, '-');

        m_load_address = stoi(address, 0, 16);
    }
}

uint64_t debugger::get_offset_load_address(uint64_t addr) {
    return addr - m_load_address;
}

dwarf::die debugger::get_func_using_pc(uint64_t pc) {

    // Iterating through all the compile units
    for(auto &compile_units: m_dwarf.compilation_units()) {
        // Checking if program counter is between DW_AT_low_pc and DW_AT_high_pc
        if(dwarf::die_pc_range(compile_units.root()).contains(pc)) {
            // Iterating through all DWARF information entries (die) in compile unit
            for(auto &die: compile_units.root()) {
                // Checking if the die is a function (as it can be variable or any other type)
                if(die.tag == dwarf::DW_TAG::subprogram) {
                    // Checking if program counter is between DW_AT_low_pc and DW_AT_high_pc
                    if(dwarf::die_pc_range(die).contains(pc)) {
                        return die;
                    }
                }
            }
        }
    }

    throw out_of_range{"Function not found!!!"};
}

dwarf::line_table::iterator debugger::get_line_entry_using_pc(uint64_t pc) {

    for(auto &compile_units: m_dwarf.compilation_units()) {
        if(dwarf::die_pc_range(compile_units.root()).contains(pc)) {
            auto& line_table = compile_units.get_line_table();
            auto iterator = line_table.find_address(pc);

            if(iterator != line_table.end()) {
                return iterator;
            } else {
                throw out_of_range{"Line Table not found!!!"};
            }
        }
    }

    throw out_of_range{"Line Table not found!!!"};
}

void execute_debugee (const string& prog_name) {
    if (ptrace(PTRACE_TRACEME, 0, 0, 0) < 0) {
        cerr << "Error in ptrace\n";
        return;
    }
    execl(prog_name.c_str(), prog_name.c_str(), nullptr);
}

int main(int argc, char** argv) {
    
    if (argc < 2){
        cerr<<"No program name!!!";
        return -1;
    }

    auto prog_name = argv[1];

    auto pid = fork();

    if (pid == 0) {
        // Child process
        personality(ADDR_NO_RANDOMIZE);        

        // ptrace provides  a  means  by  which one process ("tracer")
        // may observe and control the execution  of  another  process ("tracee"), 
        // and examine and change the tracee's memory and registers.
        execute_debugee(prog_name);
        
    } else if (pid >= 1){
        // Parent process

        cout<<"Started debugging for process "<<pid<<" ...";
        debugger dbg{prog_name, pid};

        dbg.run();

    }
    
}
