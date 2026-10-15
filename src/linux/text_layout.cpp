#include "linux/native.hpp"
#include "formatted_text_cache.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iterator>
#include <string>

namespace eu4unicode::linux_native {
namespace {
// The value arguments below match the SysV ABI of the verified native methods.
// Rect is passed on the stack after the six integer argument registers are used.
struct Rect { int left,top,right,bottom; };
struct Vector { int x,y; };
static_assert(sizeof(Rect)==16&&sizeof(Vector)==8);
SafetyHookInline *screen_hook=nullptr,*texture_hook=nullptr,*actual_hook=nullptr;
SafetyHookInline *height_hook=nullptr,*required_hook=nullptr,*real_hook=nullptr;
SafetyHookInline *line_hook=nullptr;
thread_local FormattedTextCache cache;
std::atomic<std::uint64_t> screen_breaks=0,texture_breaks=0,measured_breaks=0;
std::atomic<std::uint64_t> row_breaks=0;

template<class T> T member(const void* object,std::size_t offset) {
    T result;std::memcpy(&result,static_cast<const char*>(object)+offset,sizeof(result));return result;
}

// Engine format parsers expect single-byte introducers even when surrounding
// localization is UTF-8. Normalize only the temporary rendering/measurement
// input. Native CString owners, localization and saved text are untouched.
struct Text {
    std::string normalized;
    std::string_view value;
    String native{};
    std::shared_ptr<const FormattedText> boundaries;
    bool unicode=false,formatted;
    explicit Text(const String* source,bool formatted,bool own=false):value(source->view()),formatted(formatted) {
        if(formatted&&value.find('\xc2')!=std::string_view::npos) {
            normalized.reserve(value.size());
            for(std::size_t offset=0;offset<value.size();) {
                const auto unit=decode(value.substr(offset));
                if(unit.valid&&unit.bytes==2&&
                   (unit.value==0xa3||unit.value==0xa4||unit.value==0xa7))
                    normalized.push_back(static_cast<char>(unit.value));
                else normalized.append(value.substr(offset,unit.bytes));
                offset+=unit.bytes;
            }
            value=normalized;
        }
        if(own&&normalized.empty()&&!value.empty()) {normalized=value;value=normalized;}
        for(std::size_t offset=0;offset<value.size();) {
            const auto unit=decode(value.substr(offset));
            if(unit.valid&&unit.value>255) {unicode=true;break;}
            offset+=unit.bytes;
        }
        native=borrowed_string(value);
        if(unicode) boundaries=formatted?cache.get(value):
            std::make_shared<const FormattedText>(value,false);
    }
    bool after(std::size_t last_byte) const {
        return boundaries&&native_wrap_after(*boundaries,value,last_byte,formatted);
    }
    bool wide(std::size_t last_byte) const noexcept {
        if(!unicode||last_byte>=value.size()) return false;
        const auto unit=decode(value.substr(native_scalar_start(value,last_byte)));
        return unit.valid&&unit.value>255;
    }
};
// Stack-owned scopes restore the enclosing call on nested native rendering.
// Cached values own their boundary data; no native String/frame is retained.
struct Scope {
    Text& text;
    Scope*& current;
    Scope* previous;
    Scope(Text& text,Scope*& current):text(text),current(current),previous(current) {current=this;}
    ~Scope() {current=previous;}
};
thread_local Scope *screen_scope=nullptr,*texture_scope=nullptr;
thread_local Scope *actual_scope=nullptr,*height_scope=nullptr,*fit_scope=nullptr;

void screen(void* font,const String* source,std::uint64_t formatting,int alignment,
            int orientation,const void* matrix,Rect rect,Vector position,
            Vector* output,int limit,bool formatted) {
    Text text(source,formatted);Scope scope(text,screen_scope);
    screen_hook->call<void>(font,&text.native,formatting,alignment,orientation,
                           matrix,rect,position,output,limit,formatted);
}
void* texture(void* font,void* context,void* target,Vector* size,Vector* position,
              const String* source,void* background,unsigned width,unsigned height,
              const Vector* padding,std::uint64_t formatting,bool flag,unsigned count,
              bool flag2,bool formatted) {
    Text text(source,formatted);Scope scope(text,texture_scope);
    return texture_hook->call<void*>(font,context,target,size,position,&text.native,
        background,width,height,padding,formatting,flag,count,flag2,formatted);
}
void actual(void* font,const String* source,int width,int height,Vector* padding,
            Vector* output,bool formatted) {
    Text text(source,formatted);Scope scope(text,actual_scope);
    actual_hook->call<void>(font,&text.native,width,height,padding,output,formatted);
}
int height(void* font,const String* source,int width,int limit,const Vector* padding,
           bool formatted) {
    Text text(source,formatted);Scope scope(text,height_scope);
    return height_hook->call<int>(font,&text.native,width,limit,padding,formatted);
}
void required(void* font,const String* source,String* output,int width,int height,
              Vector* padding,bool formatted) {
    Text text(source,formatted,source==output);
    Scope scope(text,fit_scope);
    required_hook->call<void>(font,&text.native,output,width,height,padding,formatted);
}
void real(void* font,const String* source,String* output,int width,int height,
          Vector* padding,bool formatted,bool flag,bool flag2,int* lines,bool* clipped) {
    Text text(source,formatted,source==output);
    Scope scope(text,fit_scope);
    real_hook->call<void>(font,&text.native,output,width,height,padding,
                         formatted,flag,flag2,lines,clipped);
}
int line_size(const void* buffer,const String* source) {
    auto text=source->view();
    // GetTextAsRows calls this only when its first explicit line is too wide.
    // Its original byte bisection can return a UTF-8 continuation byte; that
    // remainder then becomes the next row's source, permanently losing text.
    text=text.substr(0,text.find_first_of("\r\n"));
    bool unicode=false;
    for(std::size_t offset=0;offset<text.size();) {
        const auto unit=decode(text.substr(offset));
        if(unit.valid&&unit.bytes>1) {unicode=true;break;}
        offset+=unit.bytes;
    }
    if(!unicode) return line_hook->call<int>(buffer,source);
    const bool formatted=member<bool>(buffer,0xe4);
    const auto boundaries=formatted?cache.get(text):
        std::make_shared<const FormattedText>(text,false);
    const auto& prefixes=boundaries->prefixes();
    auto font=member<void*>(buffer,0x98);
    const auto table=member<void**>(font,0);
    const auto measure=reinterpret_cast<int(*)(void*,const char*,int,bool)>(table[0x68/8]);
    const auto padding=reinterpret_cast<int(*)(void*,bool)>(table[0xb0/8])(font,true);
    const auto available=std::max(0,static_cast<int>(member<std::uint16_t>(buffer,0x68))-padding);
    // Search only complete formatted graphemes. Native glyph advances and
    // kerning remain authoritative, including the mod's bitmap metrics.
    std::size_t visible_end=0;
    while(visible_end<text.size()) {
        const auto unit=native_text_unit(text,visible_end,formatted);
        visible_end=unit.end;
        if(unit.kind!=TextUnitKind::color) break;
    }
    const auto first=std::lower_bound(prefixes.begin(),prefixes.end(),visible_end);
    auto low=first,high=prefixes.end();
    while(low!=high) {
        const auto middle=low+(high-low)/2;
        if(measure(font,text.data(),static_cast<int>(*middle),formatted)<=available) low=middle+1;
        else high=middle;
    }
    // A box narrower than its first grapheme must still consume one cluster.
    auto selected=low==first?first:std::prev(low);
    if(selected==prefixes.end()) return 0;
    const auto fitted=selected;
    while(selected!=first&&!boundaries->line_before(*selected)) --selected;
    if(!boundaries->line_before(*selected)) selected=fitted;
    ++row_breaks;
    return static_cast<int>(*selected);
}
void screen_wrap(safetyhook::Context& c) {
    if(!screen_scope||!screen_scope->text.unicode) return;
    const auto index=static_cast<std::uint32_t>(c.r13);
    if(screen_scope->text.after(index)) {
        ++screen_breaks;c.rip=0x20523ca;
    } else if(screen_scope->text.wide(index)) {
        // Keep punctuation/combining sequences with the preceding glyph. The
        // native emergency character wrap must not split an ICU word segment.
        c.rip=0x2052620;
    }
}
void texture_wrap(safetyhook::Context& c) {
    if(texture_scope&&texture_scope->text.after(static_cast<std::uint32_t>(c.r14))) {
        ++texture_breaks;c.rip=0x204f10a;
    }
}
void actual_wrap(safetyhook::Context& c) {
    if(actual_scope&&actual_scope->text.after(static_cast<std::uint32_t>(c.r13))) {
        ++measured_breaks;
        std::memcpy(&c.rdx,reinterpret_cast<void*>(c.rsp+0x18),sizeof(c.rdx));
        c.rip=0x20563a6;
    }
}
void height_boundary(safetyhook::Context& c) {
    if(height_scope&&height_scope->text.after(static_cast<std::uint32_t>(c.rbp))) {
        ++measured_breaks;
        c.xmm0={};c.rip=0x2053ead;
    }
}
void substring(safetyhook::Context& c) {
    // Only the native font fitting/truncation call sites below use this hook.
    // Length is exclusive; align it to a complete formatted grapheme prefix.
    if(!fit_scope||!fit_scope->text.boundaries) return;
    const auto source=reinterpret_cast<const String*>(c.rsi)->view();
    const auto start=static_cast<std::int32_t>(c.rdx);
    const auto length=static_cast<std::int32_t>(c.rcx);
    if(start<0||length<0||static_cast<std::size_t>(start)>source.size()) return;
    const auto& boundaries=fit_scope->text.boundaries;
    const auto begin=boundaries->prefix(static_cast<std::size_t>(start));
    const auto end=boundaries->prefix(std::min(source.size(),
        static_cast<std::size_t>(start)+static_cast<std::size_t>(length)));
    c.rdx=begin;c.rcx=end-begin;
}
}
void prepare_text_layout(Hooks& hooks) {
    screen_hook=&hooks.add_inline(0x20519f8,reinterpret_cast<void*>(screen));
    texture_hook=&hooks.add_inline(0x204e886,reinterpret_cast<void*>(texture));
    actual_hook=&hooks.add_inline(0x2055f92,reinterpret_cast<void*>(actual));
    height_hook=&hooks.add_inline(0x2053b88,reinterpret_cast<void*>(height));
    required_hook=&hooks.add_inline(0x20565aa,reinterpret_cast<void*>(required));
    real_hook=&hooks.add_inline(0x2056d66,reinterpret_cast<void*>(real));
    line_hook=&hooks.add_inline(0x20d6b06,reinterpret_cast<void*>(line_size));
    for(const auto target:{0x20523ca,0x2052620,0x204f10a,0x20563a6,0x2053ead})
        verify_site(target);
    hooks.add_mid(0x2052239,screen_wrap);
    hooks.add_mid(0x204f0f6,texture_wrap);
    hooks.add_mid(0x2056386,actual_wrap);
    hooks.add_mid(0x2053e96,height_boundary);
    for(const auto site:{0x2056be0,0x2056c3b,0x205714b,0x205730f,0x2057363,
                        0x2057631,0x20576fb,0x2057736,0x2057784,0x205781b,
                        0x2057871,0x2057935})
        hooks.add_mid(site,substring);
}
void report_layout_counts() noexcept {
    log("Text layout screen-boundaries=%llu texture-boundaries=%llu measured-boundaries=%llu row-boundaries=%llu",
        static_cast<unsigned long long>(screen_breaks.load()),
        static_cast<unsigned long long>(texture_breaks.load()),
        static_cast<unsigned long long>(measured_breaks.load()),
        static_cast<unsigned long long>(row_breaks.load()));
}
}
