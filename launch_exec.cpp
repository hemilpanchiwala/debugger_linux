#include <iostream>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/personality.h>
#include <fcntl.h>
#include <unistd.h>
#include <bits/stdc++.h>

#include "linenoise/linenoise.hpp"
#include "include/helper.h"
#include "include/breakpoint.h"
#include "include/registers.h"
#include "include/symbol.h"
#include "dwarf/dwarf++.hh"
#include "elf/elf++.hh"

using namespace std;

class ptrace_expr_context : public dwarf::expr_context {

    public:
        ptrace_expr_context (pid_t pid, uint64_t load_addr) : m_pid{pid}, m_load_addr{load_addr} {}

        dwarf::taddr reg(unsigned regnum) override {
            return get_register_value_from_dwarf_register(m_pid, regnum);
        }

        dwarf::taddr pc() {
            struct user_regs_struct registers;
            ptrace(PTRACE_GETREGS, m_pid, nullptr, &registers);
            return registers.rip - m_load_addr;
        }

        dwarf::taddr deref_size(dwarf::taddr address, unsigned size) override {
            return ptrace(PTRACE_PEEKDATA, m_pid, address + m_load_addr, nullptr);
        }

    private:
        pid_t m_pid;
        uint64_t m_load_addr;

};

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
        void set_bp_at_func(string name);
        void set_bp_at_source_line(string file_name, unsigned line);
        vector<symbol> lookup_symbol(string name);
        void print_backtrace();
        void read_variables();

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
        if (args[1][0] == '0' && args[1][1] == 'x') {
            // Removing first 2 char from address as it contains 0x
            string addr {args[1], 2};

            // Taking first 16 bytes from the address
            auto m_addr = stol(addr, 0, 16);
            addBreakpoint(m_addr);
        } else if (args[1].find(':') != string::npos) {
            // This is for line number breakpoint: <filename>:<line>
            auto file_and_line = split(args[1], ':');
            set_bp_at_source_line(file_and_line[0], stoi(file_and_line[1]));
        } else {
            // This is for setting breakpoint on function
            set_bp_at_func(args[1]);
        }
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
    } else if (is_prefix(input_command, "symbol")) {
        auto symbols = lookup_symbol(args[1]);
        for(auto& symbol: symbols) {
            cout<<symbol.name<<" "<<to_string(symbol.type)<<" address 0x"<<hex<<symbol.address<<endl;
        }
    } else if (is_prefix(input_command, "backtrace")) {
        print_backtrace();
    } else if (is_prefix(input_command, "variables")) {
        read_variables();
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

void debugger::set_bp_at_func(string name) {

    for(auto& compile_unit: m_dwarf.compilation_units()) {
        for(auto& die: compile_unit.root()) {
            if(die.has(dwarf::DW_AT::name) && (dwarf::at_name(die) == name)) {
                auto low_pc = dwarf::at_low_pc(die);
                auto line_entry = get_line_entry_using_pc(low_pc);
                // Here, before starting function there is a prologue which needs to be skipped
                line_entry++;
                addBreakpoint(get_offset_dwarf_address(line_entry->address));
            }
        }
    }

}

void debugger::set_bp_at_source_line(string file_name, unsigned line) {

    for(auto& compile_unit: m_dwarf.compilation_units()) {
        if(is_suffix(file_name, dwarf::at_name(compile_unit.root()))) {
            auto line_table = compile_unit.get_line_table();
            for(auto& line_entry: line_table) {
                // is_stmt -> check that line table entry is marked as the beginning of a statement
                if(line_entry.is_stmt && (line_entry.address == line)) {
                    addBreakpoint(get_offset_dwarf_address(line_entry.address));
                    return;
                }
            }
        }
    }

}

vector<symbol> debugger::lookup_symbol(string name) {

    vector<symbol> symbols;

    for(auto& section: m_elf.sections()) {
        // Symbol tables can only be present in sections of type DYNSYM or SYMTAB
        if ((section.get_hdr().type == elf::sht::dynsym) || section.get_hdr().type == elf::sht::symtab) {

            for(auto sym: section.as_symtab()) {
                if (sym.get_name() == name) {
                    auto& data = sym.get_data();
                    symbols.push_back(symbol{map_elf_symbol_to_struct_symbol_type(data.type()), sym.get_name(), data.value});
                }
            }

        }
    }

    return symbols;

}

void debugger::print_backtrace() {

    // Lambda expression for printing frame
    auto output_frame = [frame_np = 0] (auto&& func) mutable {
        cout<<"Frame number: #"<<(frame_np++)<<" 0x"<<dwarf::at_low_pc(func)<<" "<<dwarf::at_name(func)<<endl;
    };

    auto current_func = get_func_using_pc(get_offset_load_address(get_program_counter()));
    output_frame(current_func);

    auto frame_pointer = get_register_value_from_type(m_pid, register_type::rbp);
    auto return_address = read_memory(frame_pointer + 8);

    while(dwarf::at_name(current_func) != "main") {
        current_func = get_func_using_pc(get_offset_load_address(return_address));
        output_frame(current_func);
        frame_pointer = read_memory(frame_pointer);
        return_address = read_memory(frame_pointer + 8);
    }

}

void debugger::read_variables() {
    using namespace dwarf;

    auto func = get_func_using_pc(get_offset_program_counter());

    for(auto& die: func) {
        if(die.tag == DW_TAG::variable) {
            auto location = die[DW_AT::location];

            if(location.get_type() == value::type::exprloc) {
                ptrace_expr_context context {m_pid, m_load_address};
                auto result = location.as_exprloc().evaluate(&context);

                switch (result.location_type) {
                    case expr_result::type::address:
                    {
                        auto val = read_memory(result.value);
                        cout<<at_name(die)<<" Address: 0x"<<hex<<result.value<<" Value: "<<val<<endl;
                        break;
                    }
                    case expr_result::type::reg:
                    {
                        auto val = get_register_value_from_dwarf_register(m_pid, result.value);
                        cout<<at_name(die)<<" Address: 0x"<<hex<<result.value<<" Value: "<<val<<endl;
                        break;
                    }
                    default:
                        throw runtime_error {"Unhandled variable location!!!"};
                }

            }
        }
    }
}

void execute_debugee(const string& prog_name) {
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
