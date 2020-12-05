#include <iostream>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/personality.h>
#include <unistd.h>
#include <bits/stdc++.h>

#include "linenoise.hpp"
#include "helper.h"
#include "breakpoint.h"

using namespace std;

class debugger {

    public:
        debugger(string prog_name, pid_t pid) : m_prog_name{move(prog_name)}, m_pid{pid} {}

        void run();
        void runCommand(const string& line);
        void continue_execution();
        void addBreakpoint(intptr_t addr);

    private:
        string m_prog_name;
        pid_t m_pid;
        unordered_map<intptr_t, breakpoint> addr_to_bp;

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
    } else {
        cerr<<"No command found!! \n";
    }

}

void debugger::continue_execution() {

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
