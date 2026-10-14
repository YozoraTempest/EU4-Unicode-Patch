#include "linux/native.hpp"
#include "unicode_text.hpp"
#include "glyph_registry.hpp"
#include <algorithm>
#include <cstring>

namespace eu4unicode::linux_native {
namespace {
SafetyHookInline* parse_hook=nullptr;
SafetyHookInline* destroy_hook=nullptr;
template<class T> T read(std::uintptr_t address) noexcept {
    T result;std::memcpy(&result,reinterpret_cast<void*>(address),sizeof(T));return result;
}
Scalar scalar(const char* text,std::size_t remaining) noexcept {
    auto result=decode({text,std::min<std::size_t>(4,remaining)});
    if(!result.valid) return {static_cast<unsigned char>(*text),1,false};
    return result;
}
Scalar scalar_z(const char* text) noexcept {
    std::size_t bytes=0;
    while(bytes<4&&text[bytes]) ++bytes;
    return bytes?scalar(text,bytes):Scalar{0,1,true};
}
NativeGlyph* glyph(std::uintptr_t table,std::uint32_t code) noexcept {
    auto entries=reinterpret_cast<void* const*>(table);
    auto found=code<=255?entries[code]:find_unicode_glyph(entries,code);
    return static_cast<NativeGlyph*>(found);
}
void parse(void* font) {
    parse_hook->call<void>(font);
    const auto table=reinterpret_cast<void* const*>(reinterpret_cast<std::uintptr_t>(font)+0x100);
    bind_unicode_font(table);
    const auto usage=unicode_glyph_usage();
    if(usage.glyphs) log("Font parsed asset=%s unicode-fonts=%zu unicode-glyphs=%zu",
        reinterpret_cast<String*>(reinterpret_cast<std::uintptr_t>(font)+0xc0)->bytes,
        usage.fonts,usage.glyphs);
}
void destroy(void* table) {
    release_unicode_font(static_cast<void* const*>(table));
    destroy_hook->call<void>(table);
}
void load_glyph(safetyhook::Context& c) {
    const auto code=static_cast<std::uint32_t>(c.r13);
    if(code<=255) return;
    auto table=reinterpret_cast<void* const*>(read<std::uintptr_t>(c.rsp+8)+0x100);
    if(code<=0x10ffff&&(code<0xd800||code>0xdfff)) {
        if(!find_unicode_glyph(table,code)) if(auto entry=allocate_unicode_glyph(table,code)) {
            *entry={static_cast<std::int16_t>(read<int>(c.rsp+0x14)),
                static_cast<std::int16_t>(read<int>(c.rsp+0x10)),
                static_cast<std::int16_t>(c.r12),static_cast<std::int16_t>(c.rbx),
                static_cast<std::int16_t>(c.r14),static_cast<std::int16_t>(c.rbp),
                static_cast<std::int16_t>(c.r15),0,0};
        }
    }
    // Every metric has already been read. Higher glyphs belong to the shared
    // registry; bypass the native fixed array and its allocation/destruction.
    c.rip=0x204e0aa;
}
void width(safetyhook::Context& c) {
    const auto i=static_cast<std::uint32_t>(c.r14);
    const auto n=static_cast<std::uint32_t>(c.rdx);
    if(i>=n) return;
    const auto unit=scalar(reinterpret_cast<char*>(c.rbx+i),n-i);
    if(unit.bytes==1) return;
    c.r15=i;c.r14+=unit.bytes-1;c.rsi=unit.value;
    if(unit.value<=255) c.rip=0x2053fde;
    else {c.rbp=reinterpret_cast<std::uintptr_t>(glyph(c.rdi+0x100,unit.value));c.rip=0x205402f;}
}
void screen_measure(safetyhook::Context& c) {
    const auto i=static_cast<std::uint32_t>(c.r13);
    const auto n=static_cast<std::uint32_t>(c.r15);
    if(i>=n) return;
    const auto source=reinterpret_cast<char*>(c.r14+i);
    const auto unit=scalar(source,n-i);
    if(unit.bytes==1) return;
    const auto destination=static_cast<std::uint32_t>(c.rbx);
    const auto count=unit.value>255?unit.bytes:1;
    if(destination+count>=256) {
        // Retain the native scratch-buffer bound without splitting a scalar.
        c.r13=n-1;c.r12=destination;c.rbp=0;c.rcx=0;c.rip=0x2052191;return;
    }
    auto buffer=reinterpret_cast<char*>(0x3345190+destination);
    if(unit.value>255) std::memcpy(buffer,source,count);
    else *buffer=static_cast<char>(unit.value);
    c.r12=destination+count;c.rax=destination;c.r13=i+unit.bytes-1;c.rcx=unit.value;
    if(unit.value<=255) {c.rbp=c.r13;c.rip=0x205212b;}
    else {
        c.rdx=read<std::uintptr_t>(c.rsp+0x30);
        c.rbp=reinterpret_cast<std::uintptr_t>(glyph(c.rdx+0x100,unit.value));
        c.rip=0x2052191;
    }
}
void screen_draw(safetyhook::Context& c) {
    const auto unit=scalar_z(reinterpret_cast<char*>(0x333d450+c.rbx));
    if(unit.bytes==1) return;
    c.r15+=unit.bytes-1;
    c.rdi=read<std::uintptr_t>(c.rsp+0x30);
    c.r13=reinterpret_cast<std::uintptr_t>(glyph(c.rdi+0x100,unit.value));
    c.rip=0x205308c;
}
// CString indexing returns a pointer to the current byte at these sites.
// The loop counter stays a byte offset and skips continuation bytes, while
// native geometry still receives one glyph record for the complete scalar.
void actual_size(safetyhook::Context& c) {
    const auto unit=scalar_z(reinterpret_cast<char*>(c.rax));
    if(unit.bytes==1) return;
    c.r13+=unit.bytes-1;c.rcx=read<std::uintptr_t>(c.rsp+0x28);
    c.rbp=reinterpret_cast<std::uintptr_t>(glyph(c.rcx+0x100,unit.value));c.rip=0x20561a8;
}
void required_size(safetyhook::Context& c) {
    const auto unit=scalar_z(reinterpret_cast<char*>(c.rax));
    if(unit.bytes==1) return;
    *reinterpret_cast<int*>(c.rsp+4)+=static_cast<int>(unit.bytes-1);
    c.rbp=reinterpret_cast<std::uintptr_t>(glyph(c.r13+0x100,unit.value));c.rip=0x205687d;
}
void real_size(safetyhook::Context& c) {
    const auto unit=scalar_z(reinterpret_cast<char*>(c.rax));
    if(unit.bytes==1) return;
    *reinterpret_cast<int*>(c.rsp)+=static_cast<int>(unit.bytes-1);
    c.rbp=reinterpret_cast<std::uintptr_t>(glyph(c.r15+0x100,unit.value));c.rip=0x20570c1;
}
void vertices_measure(safetyhook::Context& c) {
    const auto unit=scalar_z(reinterpret_cast<char*>(c.rax));
    if(unit.bytes==1) return;
    c.r14+=unit.bytes-1;c.rcx=read<std::uintptr_t>(c.rsp+0x68);
    c.r15=reinterpret_cast<std::uintptr_t>(glyph(c.rcx+0x100,unit.value));c.rip=0x2055572;
}
void vertices_copy(safetyhook::Context& c) {
    const auto source=reinterpret_cast<char*>(c.rax);
    const auto unit=scalar_z(source);
    if(unit.bytes==1) return;
    const std::string text(source,unit.bytes);
    using Constructor=void(*)(void*,const char*);
    c.rbx=c.rsp+0x10;
    reinterpret_cast<Constructor>(0x254b1c2)(reinterpret_cast<void*>(c.rbx),text.c_str());
    c.rip=0x205553b;
}
void vertices_draw(safetyhook::Context& c) {
    const auto unit=scalar_z(reinterpret_cast<char*>(c.rax));
    if(unit.bytes==1) return;
    c.rbx+=unit.bytes-1;
    c.rax=reinterpret_cast<std::uintptr_t>(glyph(c.r12+0x100,unit.value));
    c.rip=0x2055acc;
}
void map_draw(safetyhook::Context& c) {
    const auto unit=scalar_z(reinterpret_cast<char*>(c.rax));
    if(unit.bytes==1) return;
    c.r14+=unit.bytes-1;c.rax=unit.value;
    c.r13=reinterpret_cast<std::uintptr_t>(glyph(c.r12,unit.value));c.rip=0x1b5f46a;
}
void nudged_count(safetyhook::Context& c) {
    const auto unit=scalar_z(reinterpret_cast<char*>(c.rax));
    if(unit.bytes==1) return;
    c.rbx+=unit.bytes-1;
    c.rax=reinterpret_cast<std::uintptr_t>(glyph(c.r12+0x100,unit.value));
    c.rip=0x1b5fe4c;
}
}
void prepare_bitmap_fonts(Hooks& hooks) {
    parse_hook=&hooks.add_inline(0x204de66,reinterpret_cast<void*>(parse));
    destroy_hook=&hooks.add_inline(0x204d114,reinterpret_cast<void*>(destroy));
    hooks.add_mid(0x204e366,load_glyph);
    hooks.add_mid(0x2053fd9,width);
    hooks.add_mid(0x2052112,screen_measure);
    hooks.add_mid(0x2052ed5,screen_draw);
    hooks.add_mid(0x2056198,actual_size);
    hooks.add_mid(0x2056872,required_size);
    hooks.add_mid(0x20570b6,real_size);
    hooks.add_mid(0x2055562,vertices_measure);
    verify_site(0x254b1c2);
    hooks.add_mid(0x205552b,vertices_copy);
    hooks.add_mid(0x2055ac1,vertices_draw);
    hooks.add_mid(0x1b5f463,map_draw);
    // AddNudgedNames sizes a stack vertex buffer with a separate glyph pass.
    // Its count must use the same scalars as FillVertexBuffer.
    hooks.add_mid(0x1b5fe41,nudged_count);
}
}
