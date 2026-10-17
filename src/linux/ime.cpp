#include "linux/native.hpp"
#include "linux/editor_layout.hpp"
#include "linux/ime_presentation.hpp"
#include "linux/font_pages.hpp"
#include "linux/paragraph.hpp"
#include "editor_document.hpp"
#include "formatted_text.hpp"
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
struct Projection {
    void* owner=nullptr;
    std::string text;
    int width=0,caret=0,delta=0;
    int begin=0,end=0,selection_begin=0,selection_end=0;
    std::vector<ImePresentation::Range> underlines,selections;
    bool document_selection=false;
} projection;
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
    const auto font=member<void*>(buffer,0x98);
    const auto font_table=*static_cast<void***>(font);
    const auto graphics=*reinterpret_cast<void**>(0x3349508);
    reinterpret_cast<void(*)(void*,int*,int*)>(0x207c56c)(graphics,&xy[0],&xy[1]);
    // CTextBuffer +0x1d0 returns a cached cursor position, not text metrics.
    // Query the same font width/height methods as the native cursor code.
    const auto height=reinterpret_cast<int(*)(void*)>(font_table[0x70/8])(font);
    int wh[2]={1,std::max(1,height)};
    reinterpret_cast<void(*)(void*,int*,int*)>(0x207c56c)(graphics,&wh[0],&wh[1]);
    if(projection.owner==focused&&(!preedit.empty()||projection.document_selection)) {
        const auto pixels=[&](int value) {
            int size[2]={value,0};
            reinterpret_cast<void(*)(void*,int*,int*)>(0x207c56c)(graphics,&size[0],&size[1]);
            return size[0];
        };
        // The verified cursor-position call site has already applied the
        // display viewport's advance. The cursor image includes transparent
        // offsets, so decorations must use the text sprite's content origin.
        const auto sprite=member<void*>(focused,0x1e8);
        const auto sprite_table=*static_cast<void***>(sprite);
        const auto origin=reinterpret_cast<Point(*)(void*)>(sprite_table[0x178/8])(sprite);
        if(!coordinate(origin.x)||!coordinate(origin.y)) return;
        const auto vertical=sprite_table[0x178/8]==reinterpret_cast<void*>(0x208c29e)?member<float>(sprite,0x254):0;
        int content[2]={static_cast<int>(origin.x)+member<int>(focused,0x240)+member<int>(font,0x30),
            static_cast<int>(origin.y+vertical)+member<int>(focused,0x244)+member<int>(font,0x34)};
        reinterpret_cast<void(*)(void*,int*,int*)>(0x207c56c)(graphics,&content[0],&content[1]);
        // Publish values, not an editor pointer, for the native draw thread.
        ImePresentation display;
        display.sprite=sprite;display.text=projection.text;
        display.x=content[0];display.y=content[1];
        display.width=pixels(projection.width);display.height=std::max(1,wh[1]);
        display.underline_begin=pixels(projection.begin);display.underline_end=pixels(projection.end);
        display.selection_begin=pixels(projection.selection_begin);display.selection_end=pixels(projection.selection_end);
        for(const auto& range:projection.underlines) display.underlines.push_back({pixels(range.begin),pixels(range.end)});
        for(const auto& range:projection.selections) display.selections.push_back({pixels(range.begin),pixels(range.end)});
        display.thickness=std::max(1,display.height/16);
        publish_ime_presentation(std::move(display));
        if(!preedit.empty()) {xy[0]=content[0]+pixels(projection.caret);xy[1]=content[1];}
    }
    set_rect({xy[0],xy[1],std::max(1,wh[0]),std::max(1,wh[1])});
}
void refresh() {
    if(!focused||refreshing) return;
    refreshing=true;
    struct Reset {~Reset(){refreshing=false;}} reset;
    reinterpret_cast<void(*)(void*)>(0x2066944)(focused);
    EditorLayoutScope scope(focused);
    cursor_hook->call<void>(focused);
    update_rect();
}
void clear() {
    projection={};clear_ime_presentation();clear_editor_view();
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
    forget_editor_layout(editor);
    if(focused==editor) {
        focused=nullptr;preedit.clear();preedit_cursor=preedit_selection=0;
        projection={};clear_ime_presentation();clear_editor_view();
    }
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
    EditorLayoutScope scope(editor);
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
                    if(preedit.empty()) clear();
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
std::string color_at(std::string_view text,std::size_t position,bool formatted) {
    std::string color;
    if(formatted) for(std::size_t offset=0;offset<position;) {
        const auto unit=native_text_unit(text,offset,true);
        if(unit.kind==TextUnitKind::color) color=text.substr(unit.begin,unit.end-unit.begin);
        offset=unit.end;
    }
    return color;
}
void preview_text(safetyhook::Context& context) {
    if(preedit.empty()||reinterpret_cast<void*>(context.r14)!=focused||member<bool>(focused,0x262)) return;
    projection={};clear_ime_presentation();clear_editor_view();
    auto row=reinterpret_cast<String*>(context.rsp+0x38);
    auto buffer=static_cast<char*>(focused)+0xc8;
    const auto column=member<std::uint16_t>(buffer,0x54);
    const auto source=reinterpret_cast<const String*>(buffer+0x30)->view();
    if(column>row->size||source.size()>32000||!valid_utf8(source)||!valid_utf8(row->view())) return;
    const auto cursor=reinterpret_cast<int(*)(void*)>(0x20d602c)(buffer);
    if(cursor<0||static_cast<std::size_t>(cursor)>source.size()) return;
    auto anchor=cursor;
    if(member<std::uint64_t>(buffer,0x78))
        anchor=reinterpret_cast<int(*)(void*,int,int)>(0x20d55d2)(buffer,
            member<std::uint16_t>(buffer,0x94),member<std::uint16_t>(buffer,0x92));
    if(anchor<0||static_cast<std::size_t>(anchor)>source.size()) return;
    const auto original=align_selection(source,anchor,cursor);
    const auto selected=align_selection(preedit,preedit_cursor,preedit_cursor+preedit_selection);
    // SDL's nonempty range denotes highlighted feedback. Keep its active end
    // visible; a zero-length range is the insertion caret reported by SDL.
    const auto composed=composition_preview({std::string(source),original},{preedit,selected.caret});
    const auto insertion=std::min(original.anchor,original.caret);
    const auto highlighted=align_selection(composed.text,insertion+selected.anchor,insertion+selected.caret);
    const auto newline=composed.caret?composed.text.find_last_of("\r\n",composed.caret-1):std::string::npos;
    const auto line_begin=newline==std::string::npos?0:newline+1;
    const auto newline_end=composed.text.find_first_of("\r\n",composed.caret);
    const auto line_end=newline_end==std::string::npos?composed.text.size():newline_end;
    const auto line=std::string_view(composed.text).substr(line_begin,line_end-line_begin);
    const bool formatted=member<bool>(buffer,0xe4);
    const auto font=member<void*>(buffer,0x98);
    const auto table=*static_cast<void***>(font);
    const auto width=reinterpret_cast<int(*)(void*,const char*,int,bool)>(table[0x68/8]);
    const auto measure=[&](std::string_view text) {return width(font,text.data(),static_cast<int>(text.size()),formatted);};
    const auto padding=reinterpret_cast<int(*)(void*,bool)>(table[0xb0/8])(font,true);
    const auto available=std::max(0,static_cast<int>(member<std::uint16_t>(buffer,0x68))-padding-2);
    if(available>0) if(const auto shaped=font_paragraph_layout(font,line,32767,false,formatted)) {
        const auto scale=member<float>(font,0x940);
        const auto caret=shaped->caret(composed.caret-line_begin).x*scale;
        const auto left=std::clamp(std::round(caret)-available+1,0.f,
            std::max(0.f,std::ceil(shaped->metrics().width*scale)-available));
        Projection next;
        next.owner=focused;next.text=line;next.width=available;next.caret=static_cast<int>(std::round(caret)-left);
        next.delta=next.caret-static_cast<int>(editor_layout_x(buffer,row->view()).value_or(measure(row->view().substr(0,column))));
        const auto regions=[&](std::size_t first,std::size_t last) {
            std::vector<ImePresentation::Range> result;
            first=std::clamp(first,line_begin,line_end)-line_begin;
            last=std::clamp(last,line_begin,line_end)-line_begin;
            for(const auto& region:shaped->selection(std::min(first,last),std::max(first,last))) {
                const auto begin=static_cast<int>(std::round(region.x*scale)-left);
                const auto end=static_cast<int>(std::round((region.x+region.width)*scale)-left);
                result.push_back({begin,end});
            }
            return result;
        };
        next.underlines=regions(composed.begin,composed.end);
        if(preedit_selection) next.selections=regions(highlighted.anchor,highlighted.caret);
        if(!next.underlines.empty()) {next.begin=next.underlines.front().begin;next.end=next.underlines.back().end;}
        if(!next.selections.empty()) {next.selection_begin=next.selections.front().begin;next.selection_end=next.selections.back().end;}
        // Shape the complete line once. Only its raster viewport is cropped;
        // slicing logical text here would change Arabic joining and bidi runs.
        publish_editor_view(font,std::string(line),{left/scale,available/scale});
        reinterpret_cast<void*(*)(void*,std::size_t,std::size_t,const char*,std::size_t)>(0x25a3f30)(
            row,0,row->size,line.data(),line.size());
        projection=std::move(next);previews.fetch_add(1,std::memory_order_relaxed);return;
    }
    const FormattedText units(line,formatted);
    const auto& edges=units.prefixes();
    const auto active=*std::lower_bound(edges.begin(),edges.end(),composed.caret-line_begin);
    const auto viewport=text_viewport(edges,active,available,[&](std::size_t begin,std::size_t end) {
        auto text=color_at(line,begin,formatted);text.append(line.substr(begin,end-begin));return measure(text);
    });
    auto text=color_at(line,viewport.begin,formatted);
    const auto style=text.size();text.append(line.substr(viewport.begin,viewport.end-viewport.begin));
    const auto mapped=[&](std::size_t position) {
        const auto first=line_begin+viewport.begin,last=line_begin+viewport.end;
        return style+(std::clamp(position,first,last)-first);
    };
    const auto advance=[&](std::size_t position) {return measure(std::string_view(text).substr(0,mapped(position)));};
    Projection next;
    next.owner=focused;next.text=text;next.width=available;next.caret=advance(composed.caret);
    next.delta=next.caret-measure(row->view().substr(0,column));
    next.begin=advance(composed.begin);next.end=advance(composed.end);
    if(preedit_selection) {
        next.selection_begin=advance(highlighted.anchor);next.selection_end=advance(highlighted.caret);
    }
    next.underlines.push_back({next.begin,next.end});
    if(preedit_selection) next.selections.push_back({next.selection_begin,next.selection_end});
    // Only the native draw call's owned scratch string is changed. The document,
    // undo state, search query and save data never receive uncommitted text.
    reinterpret_cast<void*(*)(void*,std::size_t,std::size_t,const char*,std::size_t)>(
        0x25a3f30)(row,0,row->size,text.data(),text.size());
    projection=std::move(next);
    previews.fetch_add(1,std::memory_order_relaxed);
}
void preview(safetyhook::Context& context) {
    try {preview_text(context);} catch(...) {
        projection={};clear_ime_presentation();clear_editor_view();
        static bool logged=false;if(!logged) {log("Linux IME display projection failed");logged=true;}
    }
}
void caret_position(safetyhook::Context& context) {
    if(preedit.empty()||reinterpret_cast<void*>(context.r12)!=focused||projection.owner!=focused) return;
    const auto position=reinterpret_cast<int*>(context.rsi);
    position[0]+=projection.delta;
}
void selection_position(safetyhook::Context& context) {
    if(reinterpret_cast<void*>(context.r12)!=focused) return;
    if(!preedit.empty()&&projection.owner==focused) {context.rip=0x2066f16;return;}
    if(!preedit.empty()||member<bool>(focused,0x262)) return;
    projection={};clear_ime_presentation();
    auto buffer=static_cast<char*>(focused)+0xc8;
    if(!member<std::uint64_t>(buffer,0x78)) return;
    // GetRowNo owns its CString; no engine row pointer survives this callback.
    String row{};
    reinterpret_cast<void(*)(String*,void*,int)>(0x20d4d76)(&row,buffer,member<std::uint16_t>(buffer,0x56));
    struct Destroy {String* value;~Destroy(){reinterpret_cast<void(*)(String*)>(0xd95742)(value);}} destroy{&row};
    try {
        const auto regions=editor_layout_selection(buffer,row.view());
        if(!regions) return;
        const auto font=member<void*>(buffer,0x98);
        const auto table=*static_cast<void***>(font);
        const auto padding=reinterpret_cast<int(*)(void*,bool)>(table[0xb0/8])(font,true);
        projection.owner=focused;projection.text=row.view();projection.document_selection=true;
        projection.width=std::max(0,static_cast<int>(member<std::uint16_t>(buffer,0x68))-padding-2);
        for(const auto& region:*regions) projection.selections.push_back({static_cast<int>(region.x),static_cast<int>(region.x+region.width)});
        context.rip=0x2066f16; // Hide only the native single rectangle, retaining logical selection.
    } catch(...) {projection={};}
}
}
void prepare_ime(Hooks& hooks) {
    for(const auto address:{0x2120930,0x2120950,0x2120960,0x21212a0,0x21206f0,0x2120340,0x207c56c,0x2067386,0x2097208,0x208c29e,0x20541d4,0x2066944,0x20d602c,0x20d55d2,0x2066f16}) verify_site(address);
    poll_hook=&hooks.add_inline(0x21202b0,reinterpret_cast<void*>(poll));
    focus_hook=&hooks.add_inline(0x206731a,reinterpret_cast<void*>(focus));
    blur_hook=&hooks.add_inline(0x2067420,reinterpret_cast<void*>(blur));
    destroy_hook=&hooks.add_inline(0x20662d6,reinterpret_cast<void*>(destroy));
    cursor_hook=&hooks.add_inline(0x2066cac,reinterpret_cast<void*>(cursor));
    begin_hook=&hooks.add_inline(0x1ff5d22,reinterpret_cast<void*>(begin));
    end_hook=&hooks.add_inline(0x1ff5d28,reinterpret_cast<void*>(end));
    hooks.add_mid(0x20669ff,preview);
    hooks.add_mid(0x2066df7,caret_position);
    hooks.add_mid(0x2066e11,selection_position);
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
