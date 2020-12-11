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
        void print_source(string file_name, unsigned line, unsigned context_size);
        siginfo_t get_signal_info();
        void wait_for_signal();
        void handle_bptrap(siginfo_t);
        void single_step_instruction();
        void single_step_instruction_with_bp_check();
        uint64_t read_memory(uint64_t addr);
        void write_memory(uint64_t addr, uint64_t value);
        void remove_breakpoint(intptr_t addr);
        uint64_t get_offset_program_counter();
        uint64_t get_offset_dwarf_address(uint64_t addr);
        void step_out();
        void step_in();
        void step_over();

    private:
        string m_prog_name;
        pid_t m_pid;
        unordered_map<intptr_t, breakpoint> addr_to_bp;
        dwarf::dwarf m_dwarf;
        elf::elf m_elf;
        uint64_t m_load_address;

};


void debugger::run() {

    wait_for_signal();
    initialize_load_address();

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
            cout<<"READ: "<<read_memory(stol(addr, 0, 16))<<endl;
        } else if(is_prefix(args[1], "write")) {
            string value {args[3], 2};
            write_memory(stol(addr, 0, 16), stol(value, 0, 16));
        }

    } else if (is_prefix(input_command, "stepinst")) {
        
        single_step_instruction_with_bp_check();
        auto line_entry = get_line_entry_using_pc(get_program_counter());
        print_source(line_entry->file->path, line_entry->line, 2);

    } else if (is_prefix(input_command, "step")) {
        step_in();
    } else if (is_prefix(input_command, "next")) {
        step_over();
    } else if (is_prefix(input_command, "finish")) {
        step_out();
    } else {
        cerr<<"No command found!! \n";
    }

}

uint64_t debugger::read_memory(uint64_t addr) {
    return ptrace(PTRACE_PEEKDATA, m_pid, addr, nullptr);
}

void debugger::write_memory(uint64_t addr, uint64_t value) {
    ptrace(PTRACE_POKEDATA, m_pid, addr, value);
}

void debugger::continue_execution() {

    step_over_breakpoint();
    ptrace(PTRACE_CONT, m_pid, nullptr, nullptr);

    wait_for_signal();

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

void debugger::wait_for_signal() {
    int wait_status;

    // wait for process to change state
    waitpid(m_pid, &wait_status, 0);

    auto signal = get_signal_info();

    switch(signal.si_signo) {
        case SIGTRAP:
            handle_bptrap(signal);
            break;
        case SIGSEGV:
            cout<<"Segmentation Fault caused because of "<<signal.si_code<<endl;
            break;
        default:
            cout<<"Signal: "<<signal.si_code<<endl;
    }

}

void debugger::handle_bptrap(siginfo_t sig_info) {

    switch(sig_info.si_code){
        case SI_KERNEL:
        case TRAP_BRKPT:
        {
            set_program_counter(get_program_counter() - 1);
            cout<<"Breakpoint at address 0x"<<hex<<get_program_counter()<<endl;
            auto offset = get_offset_load_address(get_program_counter());
            auto line_entry = get_line_entry_using_pc(offset);
            print_source(line_entry->file->path, line_entry->line, 2);
            break;
        }
        case TRAP_TRACE:
            break;
        default:
            cout<<"Unknown trap!!"<<endl;
    }
    return;

}

void debugger::step_over_breakpoint() {

    if(addr_to_bp.count(get_program_counter())) {
        auto& breakpoint = addr_to_bp[get_program_counter()];

        if (breakpoint.is_enabled()) {breakpoint.disable();
            ptrace(PTRACE_SINGLESTEP, m_pid, 0, nullptr);
            wait_for_signal();
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

        m_load_address = stol(address, 0, 16);
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

            if(iterator == line_table.end()) {
                throw out_of_range{"11Line Table not found!!!"};
            } else {
                return iterator;
            }
        }
    }

    throw out_of_range{"Line Table not found!!!"};
}

void debugger::print_source(string file_name, unsigned line, unsigned context_size) {

    ifstream file {file_name};

    // Defining the start and end of window around line to print
    auto start_line = (line > context_size) ? (line - context_size) : 1;
    auto end_line = line + context_size + ((context_size > line) ? (context_size - line) : 0) + 1;

    char c{};
    auto current = 1u;
    while(current != start_line && file.get(c)) {
        if(c == '\n') {
            current++;
        }
    }

    if(current == line) {
        cout<<"> ";
    }else{
        cout<<" ";
    }

    while(current <= end_line && file.get(c)) {
        cout<<c;
        if(c == '\n'){
            current++;
            cout<<(current == line) ? "> " : " ";
        }
    }

    cout<<endl;
}

siginfo_t debugger::get_signal_info() {
    siginfo_t s_info;
    ptrace(PTRACE_GETSIGINFO, m_pid, nullptr, &s_info);
    return s_info;
}

void debugger::single_step_instruction() {
    ptrace(PTRACE_SINGLESTEP, m_pid, nullptr, nullptr);
    wait_for_signal();
}

void debugger::single_step_instruction_with_bp_check() {
    if(addr_to_bp.count(get_program_counter())) {
        step_over_breakpoint();
    } else {
        single_step_instruction();
    }
}

// For stepping out, set breakpoint at return address and continue execution from there
void debugger::step_out() {

    auto frame_pointer = get_register_value_from_type(m_pid, register_type::rbp);
    auto return_address = read_memory(frame_pointer + 8);

    bool remove_bp = false;
    if(addr_to_bp.count(return_address) == 0) {
        addBreakpoint(return_address);
        remove_bp = true;
    }

    continue_execution();

    if (remove_bp) {
        remove_breakpoint(return_address);
    }

}

void debugger::remove_breakpoint(intptr_t addr) {
    if(addr_to_bp.at(addr).is_enabled()) {
        addr_to_bp.at(addr).disable();
    }
    addr_to_bp.erase(addr);
}

// For step_in, step over instruction till we reach the new line
void debugger::step_in() {
    auto line = get_line_entry_using_pc(get_offset_program_counter())->line;

    while(get_line_entry_using_pc(get_offset_program_counter())->line == line) {
        single_step_instruction_with_bp_check();
    }

    auto line_entry = get_line_entry_using_pc(get_offset_program_counter());
    print_source(line_entry->file->path, line_entry->line, 2);
}

uint64_t debugger::get_offset_program_counter() {
    return get_offset_load_address(get_program_counter());
}

uint64_t debugger::get_offset_dwarf_address(uint64_t addr) {
    return addr + m_load_address;
}

// For step over, setting a breakpoint at every line in the current function
void debugger::step_over() {

    // Get function using program counter
    auto function = get_func_using_pc(get_offset_program_counter());
    auto func_start = dwarf::at_low_pc(function);
    auto func_end = dwarf::at_high_pc(function);

    // Get the first line of function
    auto line = get_line_entry_using_pc(func_start);
    auto start_line_entry = get_line_entry_using_pc(get_offset_program_counter());

    // Vector to store the breakpoints we add for step_over (need to delete these breakpoints after step over is done)
    vector<intptr_t> bp_to_delete;

    // Iterating upto end of function and adding breakpoint which are not the start and there is no breakpoint beforehand
    while(line->address < func_end) {
        auto load_addr = get_offset_dwarf_address(line->address);
        if((line->address != start_line_entry->address) && (addr_to_bp.count(load_addr) == 0)) {
            addBreakpoint(load_addr);
            bp_to_delete.push_back(load_addr);
        }
        line++;
    }

    // Similar to step_out, adding breakpoint to return address
    auto frame_pointer = get_register_value_from_type(m_pid, register_type::rbp);
    auto return_address = read_memory(frame_pointer + 8);

    if(addr_to_bp.count(return_address) == 0) {
        addBreakpoint(return_address);
        bp_to_delete.push_back(return_address);
    }

    // Continuing the execution
    continue_execution();

    // Removing all breakpoints after step_over
    for (auto bp: bp_to_delete) {
        remove_breakpoint(bp);
    }

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
