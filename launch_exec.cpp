#include <iostream>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <unistd.h>
#include <bits/stdc++.h>

#include "linenoise.hpp"
#include "helper.h"

using namespace std;

class debugger {

    public:
        debugger(string prog_name, pid_t pid) : m_prog_name{move(prog_name)}, m_pid{pid} {}

        void run();
        void runCommand(const string& line);
        void continue_execution();

    private:
        string m_prog_name;
        pid_t m_pid;

};


void debugger::run() {
    int wait_status;

    // wait for process to change state
    waitpid(m_pid, &wait_status, 0);

    string line = nullptr;

    while(linenoise::Readline("hello> ", line)) {
        runCommand(line);
        linenoise::AddHistory(line.c_str());
    }

}

void debugger::runCommand(const string& line) {

    auto args = split(line, ' ');
    auto input_command = args[0];

    if (is_prefix(input_command, "continue")) {
        continue_execution();
    } else {
        cerr<<"No command found!! \n";
    }

}

void debugger::continue_execution() {
    ptrace(PTRACE_CONT, m_pid, nullptr, nullptr);

    int wait_status = 0;
    // wait for process to change state
    waitpid(m_pid, &wait_status, 0);

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

        // ptrace provides  a  means  by  which one process ("tracer")
        // may observe and control the execution  of  another  process ("tracee"), 
        // and examine and change the tracee's memory and registers.
        if (ptrace(PTRACE_TRACEME, 0, 0, 0) < 0) {
            cerr << "Error in ptrace\n";
            return -1;
        }

        // Executing the program
        execl(prog_name, prog_name, nullptr);
        
    } else if (pid >= 1){
        // Parent process

        cout<<"Started debugging for process "<<pid<<" ...";
        debugger dbg{prog_name, pid};

        cout<<"Hdkjejn"<<endl;
        dbg.run();
        cout<<"Hdkcsdjejn"<<endl;

    }
    
}


