#include "linux/native.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>

namespace eu4unicode::linux_native {
namespace {
struct Rect {int x,y,w,h;};
// GetScreenPosition returns two floats in the low half of XMM0 (SysV SSE).
struct Point {float x,y;};
static_assert(sizeof(Point)==8);
SafetyHookInline *poll_hook=nullptr,*focus_hook=nullptr,*blur_hook=nullptr,*cursor_hook=nullptr;
SafetyHookInline *begin_hook=nullptr,*end_hook=nullptr,*destroy_hook=nullptr;
void* focused=nullptr;
std::string preedit;
std::size_t preedit_cursor=0,preedit_selection=0;
bool refreshing=false;
bool input_active=false;
std::array<std::uint8_t,3> previous_events{};
constexpr std::array<std::uint32_t,3> text_events{0x302,0x303,0x305};
std::atomic<unsigned long long> compositions{0},commits{0},previews{0};
template<class T> T read(const void* address) {
    T value;std::memcpy(&value,address,sizeof(value));return value;
}
template<class T> T member(void* object,std::size_t offset) {
    return read<T>(static_cast<char*>(object)+offset);
}
std::size_t scalar_offset(std::string_view text,int count) {
    std::size_t offset=0;
    while(count-->0&&offset<text.size()) offset+=decode(text.substr(offset)).bytes;
    return offset;
}
void set_rect(const Rect& rect) {
    reinterpret_cast<void(*)(const Rect*)>(0x2120960)(&rect);
}
void update_rect() {
    if(!focused||!member<bool>(focused,0x260)) return;
    const auto caret=member<void*>(focused,0x1f0);
    if(!caret) return;
    const auto caret_table=*static_cast<void***>(caret);
    const auto point=reinterpret_cast<Point(*)(void*)>(caret_table[0x178/8])(caret);
    const auto coordinate=[](float value) {
        return std::isfinite(value)&&static_cast<double>(value)>=std::numeric_limits<int>::min()&&
            static_cast<double>(value)<=std::numeric_limits<int>::max();
    };
    if(!coordinate(point.x)||!coordinate(point.y)) return;
    // GetBaseCursorPosition is local to the edit box's parent. The native
    // cursor sprite has already applied parent transforms and alignment;
    // use its screen position just as GiveFocus uses the text sprite's.
    int xy[2]={static_cast<int>(point.x),static_cast<int>(point.y)};
    auto buffer=static_cast<char*>(focused)+0xc8;
    auto buffer_table=*reinterpret_cast<void***>(buffer);
    const auto graphics=*reinterpret_cast<void**>(0x3349508);
    reinterpret_cast<void(*)(void*,int*,int*)>(0x207c56c)(graphics,&xy[0],&xy[1]);
    // Native cursor dimensions are GUI coordinates; use the same conversion
    // as CEditBox::GiveFocus, preserving game UI scaling independently of VNC.
    const auto sample=borrowed_string("M");
    const auto size=reinterpret_cast<std::uint32_t(*)(void*,const String*)>(buffer_table[0x1d0/8])(
        buffer,&sample);
    int wh[2]={1,static_cast<std::uint16_t>(size>>16)};
    reinterpret_cast<void(*)(void*,int*,int*)>(0x207c56c)(graphics,&wh[0],&wh[1]);
    if(!preedit.empty()) {
        const auto prefix_text=preedit.substr(0,preedit_cursor);
        const auto prefix=borrowed_string(prefix_text);
        const auto advance=reinterpret_cast<std::uint32_t(*)(void*,const String*)>(buffer_table[0x1d0/8])(
            buffer,&prefix);
        int delta[2]={static_cast<std::uint16_t>(advance),static_cast<std::uint16_t>(advance>>16)};
        reinterpret_cast<void(*)(void*,int*,int*)>(0x207c56c)(graphics,&delta[0],&delta[1]);
        xy[0]+=delta[0];
    }
    set_rect({xy[0],xy[1],std::max(1,wh[0]),std::max(1,wh[1])});
}
void refresh() {
    if(!focused||refreshing) return;
    refreshing=true;
    reinterpret_cast<void(*)(void*)>(0x2066944)(focused);
    cursor_hook->call<void>(focused);
    refreshing=false;
    update_rect();
}
void clear() {
    if(preedit.empty()) return;
    preedit.clear();preedit_cursor=preedit_selection=0;refresh();
}
void focus(void* editor) {
    if(focused!=editor) {clear();focused=editor;}
    focus_hook->call<void>(editor);
    if(!member<bool>(editor,0x260)) focused=nullptr;
    update_rect();
}
void blur(void* editor) {
    if(focused==editor) {clear();focused=nullptr;}
    blur_hook->call<void>(editor);
}
void destroy(void* editor) {
    if(focused==editor) {focused=nullptr;preedit.clear();preedit_cursor=preedit_selection=0;}
    destroy_hook->call<void>(editor);
}
void begin(void* events,const Rect* rect) {
    begin_hook->call<void>(events,rect);
    if(!input_active) {
        const auto state=reinterpret_cast<std::uint8_t(*)(std::uint32_t,int)>(0x2120340);
        for(std::size_t i=0;i<text_events.size();++i) previous_events[i]=state(text_events[i],-1);
        input_active=true;
    }
    reinterpret_cast<void(*)()>(0x2120930)();
    if(rect) set_rect(*rect);
}
void end(void* events) {
    clear();end_hook->call<void>(events);
    if(!input_active) return;
    reinterpret_cast<void(*)()>(0x2120950)();
    // EU4 also uses character events for ordinary game shortcuts. Restore
    // their subscriptions after ending the focused editor's IME session.
    const auto state=reinterpret_cast<std::uint8_t(*)(std::uint32_t,int)>(0x2120340);
    for(std::size_t i=0;i<text_events.size();++i) state(text_events[i],previous_events[i]);
    input_active=false;
}
void cursor(void* editor) {
    cursor_hook->call<void>(editor);
    if(editor==focused) update_rect();
}
int poll(void* event) {
    while(poll_hook->call<int>(event)) {
        const auto type=read<std::uint32_t>(event);
        if(type==0x302||type==0x305) {
            const bool extended=type==0x305;
            const auto text=extended?read<const char*>(static_cast<const char*>(event)+16):static_cast<const char*>(event)+12;
            const auto limit=extended?32768u:32u;
            try {
                const auto bytes=text?strnlen(text,limit):limit;
                const auto zero=bytes<limit?text+bytes:nullptr;
                if(focused&&zero&&valid_utf8({text,static_cast<std::size_t>(zero-text)})) {
                    preedit.assign(text,zero);
                    const auto start=read<int>(static_cast<const char*>(event)+(extended?24:44));
                    const auto length=read<int>(static_cast<const char*>(event)+(extended?28:48));
                    preedit_cursor=scalar_offset(preedit,std::max(0,start));
                    preedit_selection=scalar_offset(preedit.substr(preedit_cursor),std::max(0,length));
                    compositions.fetch_add(1,std::memory_order_relaxed);refresh();
                }
            } catch(...) { log("SDL composition update failed");clear(); }
            if(extended&&text) reinterpret_cast<void(*)(void*)>(0x21212a0)(const_cast<char*>(text));
            continue;
        }
        if(type==0x303) { commits.fetch_add(1,std::memory_order_relaxed);clear(); }
        if(type==0x300&&!preedit.empty()) {
            // The IME owns navigation and cancellation until commit. Its SDL
            // editing/commit events still pass through the ordinary poll queue.
            const auto key=read<std::int32_t>(static_cast<const char*>(event)+20);
            if(key==8||key==13||key==27||key==127||(key>=0x40000049&&key<=0x40000052)) continue;
        }
        return 1;
    }
    return 0;
}
void preview(safetyhook::Context& context) {
    if(preedit.empty()||reinterpret_cast<void*>(context.r14)!=focused||member<bool>(focused,0x262)) return;
    auto row=reinterpret_cast<String*>(context.rsp+0x38);
    const auto column=member<std::uint16_t>(focused,0xc8+0x54);
    if(column>row->size||row->size>32000||preedit.size()>32000-row->size||!valid_utf8(row->view())) return;
    auto text=std::string(row->view());
    text.insert(column,preedit);
    // Only the native draw call's owned scratch string is changed. The document,
    // undo state, search query and save data never receive uncommitted text.
    reinterpret_cast<void*(*)(void*,std::size_t,std::size_t,const char*,std::size_t)>(
        0x25a3f30)(row,0,row->size,text.data(),text.size());
    previews.fetch_add(1,std::memory_order_relaxed);
}
}
void prepare_ime(Hooks& hooks) {
    for(const auto address:{0x2120930,0x2120950,0x2120960,0x21212a0,0x21206f0,0x2120340,0x207c56c,0x2067386,0x2097208,0x2066944}) verify_site(address);
    poll_hook=&hooks.add_inline(0x21202b0,reinterpret_cast<void*>(poll));
    focus_hook=&hooks.add_inline(0x206731a,reinterpret_cast<void*>(focus));
    blur_hook=&hooks.add_inline(0x2067420,reinterpret_cast<void*>(blur));
    destroy_hook=&hooks.add_inline(0x20662d6,reinterpret_cast<void*>(destroy));
    cursor_hook=&hooks.add_inline(0x2066cac,reinterpret_cast<void*>(cursor));
    begin_hook=&hooks.add_inline(0x1ff5d22,reinterpret_cast<void*>(begin));
    end_hook=&hooks.add_inline(0x1ff5d28,reinterpret_cast<void*>(end));
    hooks.add_mid(0x20669ff,preview);
}
void configure_ime() {
    // Load and configure SDL only after all startup hooks are enabled. The
    // maintained SDL2 ABI may initialize its own helper threads on first use.
    const auto hint=reinterpret_cast<int(*)(const char*,const char*)>(0x21206f0);
    const auto composition=hint("SDL_IME_INTERNAL_EDITING","0");
    const auto extended=hint("SDL_IME_SUPPORT_EXTENDED_TEXT","1");
    log("SDL IME hints: application-composition=%d extended-editing=%d",composition,extended);
}
void report_ime_counts() noexcept {
    log("SDL IME composition-events=%llu previews=%llu commits=%llu",compositions.load(),previews.load(),commits.load());
}
}
