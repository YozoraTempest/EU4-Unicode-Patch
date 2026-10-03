#include "native_steam_presence.hpp"
#include "unicode_text.hpp"
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
int assignments=0,conversions=0;
void check(bool value,const char* message) {
    if(!value) { std::cerr<<"FAIL: "<<message<<'\n';std::exit(1); }
}
eu4unicode::EngineString* assign_native(eu4unicode::EngineString* target,
                                       const char* bytes,std::uint64_t length) {
    ++assignments;
    check(target->size==0&&target->capacity==15&&target->storage.inline_bytes[0]==0,
          "fresh destination has the native empty-string layout");
    reinterpret_cast<std::string*>(target)->assign(bytes,static_cast<std::size_t>(length));
    return target;
}
eu4unicode::EngineString* convert_native(eu4unicode::EngineString* target,
                                        const eu4unicode::EngineString* source) {
    ++conversions;
    std::string value;
    for(const unsigned char byte:std::string_view(source->data(),source->size))
        value+=eu4unicode::encode(byte);
    reinterpret_cast<std::string*>(target)->assign(value);
    return target;
}
void check_presence(std::string source) {
    const auto expected=source;
    std::string target;
    const auto before=assignments;
    auto* result=eu4unicode::construct_steam_presence(
        reinterpret_cast<eu4unicode::EngineString*>(&target),
        reinterpret_cast<const eu4unicode::EngineString*>(&source),
        eu4unicode::steam_presence_conversion_return,convert_native,assign_native);
    check(result==reinterpret_cast<eu4unicode::EngineString*>(&target),"native return pointer");
    check(assignments==before+1&&conversions==0,"UTF-8 avoids Latin-1 conversion");
    check(target==expected,"Steam receives the original UTF-8 bytes");
    source.assign("changed");
    source.shrink_to_fit();
    check(target==expected,"destination owns its string after the source changes");
    const auto* native=reinterpret_cast<const eu4unicode::EngineString*>(&target);
    check(native->size==expected.size()&&native->data()[native->size]==0,
          "native byte length and terminating NUL");
}
}
int main() {
    static_assert(sizeof(std::string)==sizeof(eu4unicode::EngineString));
    check_presence("");
    check_presence("France");
    check_presence(u8"明");
    check_presence(u8"匈牙利");
    check_presence(u8"King 皇帝 of 匈牙利 against 法兰西 in 1356");
    check_presence(u8"École 日本語 한국어 𠀀 😀");
    check_presence(std::string(15,'a'));
    check_presence(std::string(16,'a'));
    check_presence(std::string(255,'a')+u8"中文");

    std::string source="\xe9",target;
    const auto before=assignments;
    eu4unicode::construct_steam_presence(reinterpret_cast<eu4unicode::EngineString*>(&target),
        reinterpret_cast<const eu4unicode::EngineString*>(&source),
        eu4unicode::steam_presence_conversion_return,convert_native,assign_native);
    check(target==u8"é"&&conversions==1&&assignments==before,
          "single-byte engine text retains native conversion");
    source=u8"明";
    eu4unicode::construct_steam_presence(reinterpret_cast<eu4unicode::EngineString*>(&target),
        reinterpret_cast<const eu4unicode::EngineString*>(&source),0x7343f3,
        convert_native,assign_native);
    check(conversions==2&&assignments==before&&target!=source,
          "the other observed caller retains its native contract");
    std::cout<<"PASS: Steam UTF-8 bytes, native string ownership and scoped conversion.\n";
}
