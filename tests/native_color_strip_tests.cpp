#include "native_color_strip.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>

namespace {
using eu4unicode::EngineString;
int assignments=0,logs=0;
bool fail_before=false,fail_after=false;
void check(bool value,const char* message) {
    if(!value) { std::cerr<<"FAIL: "<<message<<'\n';std::exit(1); }
}
EngineString* assign_native(EngineString* target,const char* bytes,std::uint64_t length) {
    ++assignments;
    check(target->size==0&&target->capacity==15&&target->storage.inline_bytes[0]==0,
          "output must be constructed before native assignment");
    if(fail_before) throw std::bad_alloc();
    char* destination=target->storage.inline_bytes;
    if(length>=16) {
        destination=static_cast<char*>(std::malloc(static_cast<std::size_t>(length)+1));
        if(!destination) throw std::bad_alloc();
        target->storage.pointer=destination;
        target->capacity=length;
    }
    std::memcpy(destination,bytes,static_cast<std::size_t>(length));
    destination[length]='\0';
    target->size=length;
    if(fail_after) throw std::bad_alloc();
    return target;
}
void record_failure(const char*) { ++logs; }
void check_native(std::string input,const std::string& expected) {
    const auto original=input;
    EngineString source{};
    source.storage.pointer=input.data();
    source.size=input.size();
    source.capacity=std::max<std::uint64_t>(16,source.size);
    const auto source_abi=source;
    EngineString target;
    std::memset(&target,0xcc,sizeof(target));
    const auto before=assignments;
    check(eu4unicode::construct_colorless_text(&source,&target)==&target,"native return pointer changed");
    check(assignments==before+1,"output must use exactly one native string assignment");
    check(std::string_view(target.data(),static_cast<std::size_t>(target.size))==expected,"native output bytes differ");
    check(target.data()[target.size]=='\0',"native output lacks its terminating NUL");
    check(input==original&&std::memcmp(&source,&source_abi,sizeof(source))==0,"source string was modified");
    input.assign("changed");
    input.shrink_to_fit();
    check(std::string_view(target.data(),static_cast<std::size_t>(target.size))==expected,"output borrowed source storage");
    if(target.capacity>=16) std::free(const_cast<char*>(target.storage.pointer));
}
}
int main() {
    eu4unicode::native_color_assign=assign_native;
    eu4unicode::native_color_log=record_failure;
    for(int repeat=0;repeat<100;++repeat) {
        check_native("","");
        check_native(u8"§J瓦兰吉卫队§!",u8"瓦兰吉卫队");
        check_native("\xa7J"+std::string(u8"瓦兰吉卫队")+"\xa7!",u8"瓦兰吉卫队");
        check_native(u8"§Y俧X§!",u8"俧X");
        check_native(u8"§Y¿Cómo estás? Â  ¿李 £adm£ @FRA§!",u8"¿Cómo estás? Â  ¿李 £adm£ @FRA");
        for(const std::size_t length:{15,16,255,4096}) {
            const std::string text(length,'x');
            check_native(text,text);
            check_native(u8"§Y"+text+u8"§!",text);
        }
        const std::string embedded("A\0B",3);
        check_native(u8"§Y"+embedded+u8"§!",embedded);
    }
    check(logs==0,"ordinary calls reported failure");
    fail_before=true;
    check_native(u8"§Y中文§!","");
    fail_before=false;
    fail_after=true;
    check_native(u8"§Y长字符串保留原生分配的所有权§!","");
    fail_after=false;
    check(logs==2,"native failures were not contained and reported");
    check_native(u8"§Y恢复§!",u8"恢复");
    std::cout<<"PASS: native color stripping, output construction, source ownership and exception boundary.\n";
}
