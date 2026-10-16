#include "linux/editor_mutation.hpp"
#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace eu4unicode::linux_native {
void log(const char*,...) noexcept {}
void verify_site(std::uintptr_t) {std::abort();}
void Hooks::add_mid(std::uintptr_t,safetyhook::MidHookFn) {std::abort();}
}
namespace {
void check(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
}
int main() {
    using namespace eu4unicode;
    using namespace eu4unicode::linux_native;
    const std::string forbidden="\"\xa7\xa4\xa3@{}/\\";
    for(const auto text:{"A سلام B","A שלום B","ạ́ हिन्दी Z","中文 English 1444","𝒜𠀀🙂"})
        check(filter_editor_input(text,forbidden)==text,"forbidden continuation bytes never remove part of a scalar");
    check(filter_editor_input("\"§¤£@{}/\\سلام",forbidden)=="سلام","whole forbidden scalars and ASCII remain excluded");
    check(filter_editor_input("Xا§Y","\xa7")=="XاY","Alef continuation A7 differs from section-sign scalar");
    check(filter_editor_input("abc","")=="abc","empty exclusion set preserves input");
    for(const auto text:{"A سلام B","ạ́ हिन्दी Z","中文 English 1444","x👩‍👩‍👧‍👦Z"}) {
        const auto boundaries=grapheme_boundaries(text);
        for(std::size_t limit=0;limit<=std::string_view(text).size()+2;++limit) {
            const auto end=editor_prefix(text,limit,false);
            check(end<=limit&&std::binary_search(boundaries.begin(),boundaries.end(),end),"every byte capacity returns a complete grapheme");
            check(valid_utf8(std::string_view(text).substr(0,end)),"every clipped result remains valid UTF-8");
        }
    }
    check(editor_prefix("A§R中",3,true)==1,"formatted capacity keeps its color marker atomic");
    check(editor_prefix("A§R中",4,true)==4,"complete formatted marker fits its original byte capacity");
    bool rejected=false;
    try {filter_editor_input(std::string_view("\xd8",1),forbidden);} catch(const std::invalid_argument&) {rejected=true;}
    check(rejected,"invalid input is not silently relabeled as valid");
    std::cout<<"Linux editor scalar filtering and capacity boundaries passed\n";
}
