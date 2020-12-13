#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <unistd.h>
#include <bits/stdc++.h>

using namespace std;

enum class register_type {
    r15, r14, r13, r12, rbp,
    rbx, r11, r10, r9, r8, rax,
    rcx, rdx, rsi, rdi, orig_rax, 
    rip, cs, eflags, rsp, ss,
    fs_base, gs_base, ds, es, fs, gs
};

struct register_desc {
    register_type r_type;
    int dwarf_reg_no;
    string name;
};

array<register_desc, 27> registers {{
    { register_type::r15, 15, "r15" },
    { register_type::r14, 14, "r14" },
    { register_type::r13, 13, "r13" },
    { register_type::r12, 12, "r12" },
    { register_type::rbp, 6, "rbp" },
    { register_type::rbx, 3, "rbx" },
    { register_type::r11, 11, "r11" },
    { register_type::r10, 10, "r10" },
    { register_type::r9, 9, "r9" },
    { register_type::r8, 8, "r8" },
    { register_type::rax, 0, "rax" },
    { register_type::rcx, 2, "rcx" },
    { register_type::rdx, 1, "rdx" },
    { register_type::rsi, 4, "rsi" },
    { register_type::rdi, 5, "rdi" },
    { register_type::orig_rax, -1, "orig_rax" },
    { register_type::rip, -1, "rip" },
    { register_type::cs, 51, "cs" },
    { register_type::eflags, 49, "eflags" },    
    { register_type::rsp, 7, "rsp" },
    { register_type::ss, 52, "ss" },
    { register_type::fs_base, 58, "fs_base" },
    { register_type::gs_base, 59, "gs_base" },
    { register_type::ds, 53, "ds" },
    { register_type::es, 50, "es" },
    { register_type::fs, 54, "fs" },
    { register_type::gs, 55, "gs" },
}};

uint64_t get_register_value_from_type(pid_t pid, register_type type) {
    user_regs_struct regs;
    ptrace(PTRACE_GETREGS, pid, nullptr, &regs);

    auto iter = find_if(begin(registers), end(registers), [type](auto&& rg) { return rg.r_type==type; });
    return *(reinterpret_cast<uint64_t*>(&regs) + (iter - begin(registers)));
}

void set_register_value(pid_t pid,  register_type type, uint64_t value) {
    user_regs_struct regs;
    ptrace(PTRACE_GETREGS, pid, nullptr, &regs);

    auto iter = find_if(begin(registers), end(registers), [type](auto&& rg) { return rg.r_type==type; });
    *(reinterpret_cast<uint64_t*>(&regs) + (iter - begin(registers))) = value;
    ptrace(PTRACE_SETREGS, pid, nullptr, &regs);
}

uint64_t get_register_value_from_dwarf_register(pid_t pid, unsigned dwarf) {
    auto iter = find_if(begin(registers), end(registers), [dwarf](auto&& rg) { return rg.dwarf_reg_no==dwarf; });

    if(iter == end(registers)) {
        cerr<<"Out of bounds!!!\n";
    }

    return get_register_value_from_type(pid, iter->r_type);
}

string get_register_name(register_type type) {
    auto iter = find_if(begin(registers), end(registers), [type](auto&& rg) { return rg.r_type==type; });

    return iter->name;
}

register_type get_register_type_from_name(string reg_name) {
    auto iter = find_if(begin(registers), end(registers), [reg_name](auto&& rg) { return rg.name==reg_name; });

    return iter->r_type;
}
