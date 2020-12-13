#include <bits/stdc++.h>
#include "../elf/elf++.hh"

using namespace std;

enum symbol_type {
    notype,
    section,
    func,
    file,
    object
};

string to_string(symbol_type type) {
    switch (type) {
        case symbol_type::notype:
            return "notype";
        case symbol_type::section:
            return "section";
        case symbol_type::func:
            return "func";
        case symbol_type::file:
            return "file";
        case symbol_type::object:
            return "object";
    }
}

symbol_type map_elf_symbol_to_struct_symbol_type(elf::stt type) {
    switch (type) {
        case elf::stt::notype:
            return symbol_type::notype;
        case elf::stt::section:
            return symbol_type::section;
        case elf::stt::func:
            return symbol_type::func;
        case elf::stt::file:
            return symbol_type::file;
        case elf::stt::object:
            return symbol_type::object;
    }
}

struct symbol {
    symbol_type type;
    string name;
    uintptr_t address;
};
