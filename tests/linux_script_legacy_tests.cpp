#include "linux/script_legacy.hpp"
#include "legacy_text.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace eu4unicode::linux_native {
void log(const char*,...) noexcept {}
SafetyHookInline& Hooks::add_inline(std::uintptr_t,void*) {std::abort();}
}
namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
}
int main() {
    using namespace eu4unicode::linux_native;
    for(const unsigned type:{2u,15u}) {
        ScriptLexer lexer{};lexer.token_type=type;lexer.previous_token=123;lexer.flags[4]=1;
        std::strcpy(lexer.text,"\x10-N\x11\x0eN\x12\xf9R\x13it");
        check(import_script_token(lexer)&&std::string(lexer.text)=="中一对絛","quoted and unquoted raw legacy tokens");
        check(lexer.previous_token==123&&lexer.token_type==type&&lexer.flags[4]==1,"lookahead and token classification preserved");
        check(import_script_token(lexer)&&std::string(lexer.text)=="中一对絛","lookahead replay does not decode twice");
        std::strcpy(lexer.text,"中文𠀀 é");check(import_script_token(lexer)&&std::string(lexer.text)=="中文𠀀 é","plain UTF-8 unchanged");
        std::strcpy(lexer.text,"\x10");check(!import_script_token(lexer)&&!lexer.text[0],"truncated token rejected atomically");
        const auto expanded=std::string(508,'\xf6')+"\x10-N";
        std::memcpy(lexer.text,expanded.c_str(),512);
        check(!import_script_token(lexer)&&!lexer.text[0],"UTF-8 expansion cannot overflow the native token");
    }
    for(const unsigned type:{12u,15u}) {
        ScriptLexer lexer{};lexer.token_type=type;lexer.previous_token=1;lexer.flags[4]=1;
        std::strcpy(lexer.text,"\x10-N\x11\x0eN");
        check(import_binary_token(lexer)&&std::string(lexer.text)=="中一","binary quoted/unquoted import");
        check(lexer.token_type==type&&lexer.previous_token==1&&lexer.flags[4]==1,"binary token metadata preserved");
        check(import_binary_token(lexer)&&std::string(lexer.text)=="中一","binary lookahead replay is idempotent");
        std::strcpy(lexer.text,"-42");check(import_binary_token(lexer)&&std::string(lexer.text)=="-42","binary numeric text unchanged");
        std::strcpy(lexer.text,"中文𠀀");check(import_binary_token(lexer)&&std::string(lexer.text)=="中文𠀀","binary UTF-8 unchanged");
        std::strcpy(lexer.text,"\x10");check(!import_binary_token(lexer)&&!lexer.text[0],"truncated binary token rejected");
        const auto expanded=std::string(508,'\xf6')+"\x10-N";
        std::memcpy(lexer.text,expanded.c_str(),512);
        check(!import_binary_token(lexer)&&!lexer.text[0],"UTF-8 binary expansion cannot overflow");
    }
    ScriptLexer untouched{};untouched.token_type=9;std::strcpy(untouched.text,"\x10-N");
    check(import_script_token(untouched)&&std::string(untouched.text)=="\x10-N","non-text token untouched");
    std::cout<<"Linux native legacy token import passed\n";
}
