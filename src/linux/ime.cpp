#include "linux/native.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace eu4unicode::linux_native {
namespace {
struct Rect {int x,y,w,h;};
SafetyHookInline *poll_hook=nullptr,*focus_hook=nullptr,*blur_hook=nullptr,*cursor_hook=nullptr;
SafetyHookInline *begin_hook=nullptr,*end_hook=nullptr,*destroy_hook=nullptr;
void* focused=nullptr;
std::string preedit;
std::size_t preedit_cursor=0,preedit_selection=0;
bool refreshing=false;
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
    int xy[2]{};
    reinterpret_cast<void(*)(int*,void*)>(0x2066f62)(xy,focused);
    auto buffer=static_cast<char*>(focused)+0xc8;
    auto buffer_table=*reinterpret_cast<void***>(buffer);
    const auto position=reinterpret_cast<std::uint32_t(*)(void*)>(buffer_table[0x60/8])(buffer);
    xy[0]+=static_cast<std::uint16_t>(position);
    xy[1]+=static_cast<std::uint16_t>(position>>16);
    const auto graphics=*reinterpret_cast<void**>(0x3349508);
    reinterpret_cast<void(*)(void*,int*,int*)>(0x207c56c)(graphics,&xy[0],&xy[1]);
    // Native cursor dimensions are GUI coordinates; use the same conversion
    // as CEditBox::GiveFocus, preserving game UI scaling independently of VNC.
    const std::string sample="M";
    const auto size=reinterpret_cast<std::uint32_t(*)(void*,const String*)>(buffer_table[0x1d0/8])(
        buffer,reinterpret_cast<const String*>(&sample));
    int wh[2]={1,static_cast<std::uint16_t>(size>>16)};
    reinterpret_cast<void(*)(void*,int*,int*)>(0x207c56c)(graphics,&wh[0],&wh[1]);
    if(!preedit.empty()) {
        const std::string prefix=preedit.substr(0,preedit_cursor);
        const auto advance=reinterpret_cast<std::uint32_t(*)(void*,const String*)>(buffer_table[0x1d0/8])(
            buffer,reinterpret_cast<const String*>(&prefix));
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
    reinterpret_cast<void(*)()>(0x2120930)();
    if(rect) set_rect(*rect);
}
void end(void* events) {
    clear();end_hook->call<void>(events);
    reinterpret_cast<void(*)()>(0x2120950)();
}
void cursor(void* editor) {
    cursor_hook->call<void>(editor);
    if(editor==focused) update_rect();
}
int poll(void* event) {
    while(poll_hook->call<int>(event)) {
        const auto type=read<std::uint32_t>(event);
        if(type==0x302) {
            const auto text=static_cast<const char*>(event)+12;
            const auto zero=static_cast<const char*>(std::memchr(text,0,32));
            if(focused&&zero&&valid_utf8({text,static_cast<std::size_t>(zero-text)})) {
                preedit.assign(text,zero);
                const auto start=read<int>(static_cast<const char*>(event)+44);
                const auto length=read<int>(static_cast<const char*>(event)+48);
                preedit_cursor=scalar_offset(preedit,std::max(0,start));
                preedit_selection=scalar_offset(preedit.substr(preedit_cursor),std::max(0,length));
                refresh();
            }
            continue;
        }
        if(type==0x303) clear();
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
    if(column>row->size||!valid_utf8(row->view())) return;
    auto text=std::string(row->view());
    text.insert(column,preedit);
    // Only the native draw call's owned scratch string is changed. The document,
    // undo state, search query and save data never receive uncommitted text.
    reinterpret_cast<void*(*)(void*,std::size_t,std::size_t,const char*,std::size_t)>(
        0x25a3f30)(row,0,row->size,text.data(),text.size());
}
}
void prepare_ime(Hooks& hooks) {
    for(const auto address:{0x2120930,0x2120950,0x2120960,0x207c56c,0x2067386,0x2066f62,0x2066944}) verify_site(address);
    poll_hook=&hooks.add_inline(0x21202b0,reinterpret_cast<void*>(poll));
    focus_hook=&hooks.add_inline(0x206731a,reinterpret_cast<void*>(focus));
    blur_hook=&hooks.add_inline(0x2067420,reinterpret_cast<void*>(blur));
    destroy_hook=&hooks.add_inline(0x20662d6,reinterpret_cast<void*>(destroy));
    cursor_hook=&hooks.add_inline(0x2066cac,reinterpret_cast<void*>(cursor));
    begin_hook=&hooks.add_inline(0x1ff5d22,reinterpret_cast<void*>(begin));
    end_hook=&hooks.add_inline(0x1ff5d28,reinterpret_cast<void*>(end));
    hooks.add_mid(0x20669ff,preview);
}
}
