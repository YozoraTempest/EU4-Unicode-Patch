#include "native_text_event.hpp"
#include "unicode_text.hpp"
#include <cstring>

namespace eu4unicode {
bool make_text_event(std::string_view text,NativeTextEvent& event) noexcept {
    event={};
    if(text.empty()||text.size()>=sizeof(event.text)||
       text.find('\0')!=std::string_view::npos||!valid_utf8(text)) return false;
    std::memcpy(event.text,text.data(),text.size());
    event.utf8_tag=native_utf8_tag;
    event.text_kind=3;
    event.type=2;
    return true;
}
std::string_view queued_utf8(const NativeTextEvent& event) noexcept {
    if(event.type!=2||event.text_kind!=3||event.utf8_tag!=native_utf8_tag) return {};
    std::size_t length=0;
    while(length<sizeof(event.text)&&event.text[length]) ++length;
    if(length==sizeof(event.text)) return {};
    const auto text=std::string_view(event.text,length);
    return valid_utf8(text)?text:std::string_view{};
}
}
