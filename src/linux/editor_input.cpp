#include "linux/native.hpp"
#include "linux/editor_layout.hpp"
#include "unicode_text.hpp"
#include "unicode_services.hpp"
#include "unicode_editor.hpp"
#include "formatted_text.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <stdexcept>

namespace eu4unicode::linux_native {
namespace {
SafetyHookInline *event_ctor=nullptr,*event_access=nullptr,*event_string=nullptr,*event_any=nullptr;
SafetyHookInline *edit_input=nullptr,*backspace_hook=nullptr,*delete_hook=nullptr;
SafetyHookInline *hit_hook=nullptr;
thread_local const void* text_event=nullptr;
thread_local std::uint32_t text_scalar=0;
std::atomic<std::uint64_t> packets{0},scalars{0},edited{0};
std::atomic<std::uint64_t> mouse_hits{0};
constexpr std::uint32_t marker=0xa0000000,marker_mask=0xe00000ff;

template<class T> T read(std::uintptr_t address) {
    T value;std::memcpy(&value,reinterpret_cast<void*>(address),sizeof(T));return value;
}
void construct_event(void* output,const void* input) {
    event_ctor->call<void>(output,input);
    // All ordinary constructors leave these three padding bytes unspecified.
    // Normalize them so only this module's verified SDL delivery can tag them.
    const std::uint32_t character=*static_cast<const unsigned char*>(input);
    std::memcpy(static_cast<char*>(output)+0x10,&character,sizeof(character));
}
const void* access_event(const void* input) {
    const auto event=event_access->call<const void*>(input);
    const auto word=read<std::uint32_t>(reinterpret_cast<std::uintptr_t>(input)+0x10);
    const auto code=(word>>8)&0x1fffff;
    text_event=nullptr;
    if(read<int>(reinterpret_cast<std::uintptr_t>(input)+0x50)==2&&
       (word&marker_mask)==(marker|0x3f)&&code<=0x10ffff&&(code<0xd800||code>0xdfff)) {
        text_event=event;text_scalar=code;
    }
    return event;
}
void* input_string(void* output,const void* input) {
    if(input!=text_event) return event_string->call<void*>(output,input);
    const auto utf8=encode(text_scalar);
    reinterpret_cast<void(*)(void*,const char*,int)>(0x254b1ea)(
        output,utf8.data(),static_cast<int>(utf8.size()));
    return output;
}
bool input_is_any(const void* input,const String* characters) {
    if(input!=text_event) return event_any->call<bool>(input,characters);
    if(text_scalar>255) return false;
    const auto byte=static_cast<unsigned char>(text_scalar);
    return event_any->call<bool>(&byte,characters);
}
bool edit(void* editor,const void* input) {
    try {
        const auto result=edit_input->call<bool>(editor,input);
        if(input==text_event) {
            const auto count=edited.fetch_add(1,std::memory_order_relaxed)+1;
            if(count<=4) log("Unicode editor event handled count=%llu accepted=%d",
                static_cast<unsigned long long>(count),result);
        }
        text_event=nullptr;
        return result;
    } catch(...) {text_event=nullptr;throw;}
}
void deliver(safetyhook::Context& c) {
    const auto bytes=reinterpret_cast<const char*>(c.rsp+0x3c);
    const auto end=static_cast<const char*>(std::memchr(bytes,0,32));
    c.rip=0x1ff5601;
    if(!end||!valid_utf8({bytes,static_cast<std::size_t>(end-bytes)})) {
        log("Rejected malformed SDL text packet");return;
    }
    // Keep the native keyboard update and the existing event-handler queue.
    // Each queued POD event stores one scalar in the text union's verified
    // padding. QueueInput copies all 88 bytes with eleven MOVSQ operations.
    auto keyboard=reinterpret_cast<void*>(c.r15);
    auto keyboard_table=*reinterpret_cast<void***>(keyboard);
    reinterpret_cast<void(*)(void*,int,int,int)>(keyboard_table[5])(
        keyboard,0x303,read<int>(c.rsp+0x44),0);
    auto handler=reinterpret_cast<void*>(c.r13);
    auto table=*reinterpret_cast<void***>(handler);
    std::string_view text(bytes,static_cast<std::size_t>(end-bytes));
    packets.fetch_add(1,std::memory_order_relaxed);
    while(!text.empty()) {
        const auto unit=decode(text);
        alignas(8) std::array<unsigned char,88> event{};
        const auto character=static_cast<unsigned char>(unit.value<128?unit.value:0x3f);
        construct_event(event.data(),&character);
        if(unit.value>=128) {
            const auto word=marker|(unit.value<<8)|0x3f;
            std::memcpy(event.data()+0x10,&word,sizeof(word));
        }
        reinterpret_cast<void(*)(void*,const void*)>(table[4])(handler,event.data());
        reinterpret_cast<void(*)(void*)>(0x1feba14)(event.data());
        scalars.fetch_add(1,std::memory_order_relaxed);
        text.remove_prefix(unit.bytes);
    }
}
std::string_view text_of(void* buffer) {
    return reinterpret_cast<const String*>(static_cast<char*>(buffer)+0x30)->view();
}
std::size_t position(void* buffer) {
    const int result=reinterpret_cast<int(*)(void*)>(0x20d602c)(buffer);
    return result<0?0:static_cast<std::size_t>(result);
}
void move_left(safetyhook::Context& c) {
    auto buffer=reinterpret_cast<void*>(c.rbx);
    const auto text=text_of(buffer);
    if(!valid_utf8(text)) return;
    const auto offset=position(buffer);
    if(offset>text.size()) return;
    const auto previous=previous_grapheme(text,offset);
    const auto column=static_cast<std::uint16_t>(c.rax);
    const auto span=offset-previous;
    if(span>column) return;
    c.rax=column-span;
    const auto value=static_cast<std::uint16_t>(c.rax);
    std::memcpy(reinterpret_cast<void*>(c.rbx+0x54),&value,sizeof(value));
    c.rip=0x20d7853;
}
void move_right(safetyhook::Context& c) {
    const auto text=text_of(reinterpret_cast<void*>(c.rbx));
    if(!valid_utf8(text)||c.rax>text.size()) return;
    c.rsi=next_grapheme(text,c.rax);c.rdi=c.rbx;c.rip=0x20d7938;
}
bool select_cluster(void* buffer,bool backwards) {
    const auto table=*reinterpret_cast<void***>(buffer);
    if(reinterpret_cast<bool(*)(void*)>(table[0x170/8])(buffer)) return false;
    const auto text=text_of(buffer);
    if(!valid_utf8(text)) return false;
    const auto offset=position(buffer);
    if(offset>text.size()) return false;
    const auto begin=backwards?previous_grapheme(text,offset):offset;
    const auto end=backwards?offset:next_grapheme(text,offset);
    if(end-begin<=1) return false;
    reinterpret_cast<void(*)(void*,int,int)>(0x20d7528)(
        buffer,static_cast<int>(begin),static_cast<int>(end));
    return true;
}
void backspace(void* buffer) {select_cluster(buffer,true);backspace_hook->call<void>(buffer);}
void erase(void* buffer) {select_cluster(buffer,false);delete_hook->call<void>(buffer);}
bool unicode_source(std::string_view text) {
    bool unicode=false;
    for(std::size_t offset=0;offset<text.size();) {
        const auto unit=decode(text.substr(offset));
        if(!unit.valid) return false;
        unicode|=unit.bytes>1;offset+=unit.bytes;
    }
    return unicode;
}
void mouse_scale(safetyhook::Context& c) {
    const auto editor=c.r12;
    if(read<bool>(editor+0x262)||!unicode_source(text_of(reinterpret_cast<void*>(editor+0xc8)))) return;
    struct Point {int x,y;};
    static_assert(sizeof(Point)==8);
    auto point=reinterpret_cast<Point*>(c.r14);
    const auto graphics=read<void*>(0x3349508);
    // The event is in screen pixels, while GetPositionAtScreen has returned
    // the text sprite's unscaled coordinates in RAX. Convert the absolute
    // event before the native subtraction, using the engine's integer rule.
    *point=reinterpret_cast<Point(*)(void*,const Point*)>(0x207c508)(graphics,point);
}
void mouse_origin(safetyhook::Context& c) {
    const auto buffer=reinterpret_cast<void*>(c.rdi);
    if(!unicode_source(text_of(buffer))) return;
    const auto editor=c.r12;
    const auto font=read<std::uintptr_t>(c.rdi+0x98);
    auto point=reinterpret_cast<int*>(c.rsi);
    // CEditBox subtracts its text sprite's outer position before this call.
    // The actual glyph origin also includes the border and font padding.
    // Restrict this adjustment to that verified non-password editor call.
    point[0]=std::max(0,point[0]-read<int>(editor+0x240)-read<int>(font+0x30));
    point[1]=std::max(0,point[1]-read<int>(editor+0x244)-read<int>(font+0x34));
}
void hit(void* buffer,const String* source,const int* point) {
    const auto text=source->view();
    if(!unicode_source(text)) return hit_hook->call<void>(buffer,source,point);
    // The native overload only sets a column in its supplied row. It rejects
    // negative points and performs no selection mutation or notification.
    if(point[0]<0||point[1]<0) return;
    try {
        if(editor_layout_hit(buffer,source,point)) {++mouse_hits;return;}
        const auto base=reinterpret_cast<std::uintptr_t>(buffer);
        const auto font=read<void*>(base+0x98);
        const bool formatted=read<bool>(base+0xe4);
        const auto table=*static_cast<void***>(font);
        const auto measure=reinterpret_cast<int(*)(void*,const char*,int,bool)>(table[0x68/8]);
        const FormattedText units(text,formatted);
        const auto column=nearest_text_boundary(units.prefixes(),point[0],[&](std::size_t end) {
            return measure(font,text.data(),static_cast<int>(end),formatted);
        });
        if(column>UINT16_MAX) throw std::out_of_range("Native editor hit exceeds its column range");
        const std::array<std::uint16_t,2> position{static_cast<std::uint16_t>(column),0};
        std::memcpy(static_cast<char*>(buffer)+0x54,position.data(),sizeof(position));
        ++mouse_hits;
    } catch(const std::exception& error) {
        static bool logged=false;if(!logged) {log("Unicode editor mouse hit rejected: %s",error.what());logged=true;}
    }
}
}
void prepare_editor_input(Hooks& hooks) {
    for(const auto address:{0x254b1ea,0x1feba14,0x20d602c,0x20d7528,0x207c508}) verify_site(address);
    event_ctor=&hooks.add_inline(0x1feb904,reinterpret_cast<void*>(construct_event));
    event_access=&hooks.add_inline(0x1feba1a,reinterpret_cast<void*>(access_event));
    event_string=&hooks.add_inline(0x1fec6f2,reinterpret_cast<void*>(input_string));
    event_any=&hooks.add_inline(0x1fec6d0,reinterpret_cast<void*>(input_is_any));
    edit_input=&hooks.add_inline(0x20674a4,reinterpret_cast<void*>(edit));
    backspace_hook=&hooks.add_inline(0x20d8210,reinterpret_cast<void*>(backspace));
    delete_hook=&hooks.add_inline(0x20d8480,reinterpret_cast<void*>(erase));
    hit_hook=&hooks.add_inline(0x20d5142,reinterpret_cast<void*>(hit));
    hooks.add_mid(0x1ff52d7,deliver);
    hooks.add_mid(0x20d784d,move_left);
    hooks.add_mid(0x20d7932,move_right);
    hooks.add_mid(0x2066869,mouse_scale);
    hooks.add_mid(0x2066902,mouse_origin);
}
void report_input_counts() noexcept {
    log("SDL text packets=%llu scalars=%llu editor-events=%llu mouse-hits=%llu",
        static_cast<unsigned long long>(packets.load()),static_cast<unsigned long long>(scalars.load()),
        static_cast<unsigned long long>(edited.load()),static_cast<unsigned long long>(mouse_hits.load()));
}
}
