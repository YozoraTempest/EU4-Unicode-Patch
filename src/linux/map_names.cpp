#include "linux/native.hpp"
#include "unicode_text.hpp"

namespace eu4unicode::linux_native {
namespace {
void count(safetyhook::Context& context,std::uintptr_t next) {
    const auto text=reinterpret_cast<const String*>(context.rdi)->view();
    if(!valid_utf8(text)) return;
    std::size_t characters=0;
    auto rest=text;
    while(!rest.empty()) { rest.remove_prefix(decode(rest).bytes);++characters; }
    context.rax=characters;context.rip=next;
}
void label_count(safetyhook::Context& context) { count(context,0x1b5f32c); }
void curve_count(safetyhook::Context& context) { count(context,0x1b5f337); }
void interpolation_count(safetyhook::Context& context) { count(context,0x1b5f552); }
void character(safetyhook::Context& context) {
    auto text=reinterpret_cast<const String*>(context.rdi)->view();
    if(!valid_utf8(text)) return;
    std::size_t offset=0;
    for(std::size_t index=0;index<context.rsi&&!text.empty();++index) {
        const auto bytes=decode(text).bytes;offset+=bytes;text.remove_prefix(bytes);
    }
    // CurveText's loop and interpolation use a character ordinal. Only the
    // native CString indexing call receives a byte position into the source.
    context.rsi=offset;
}
}
void prepare_map_names(Hooks& hooks) {
    hooks.add_mid(0x1b5f327,label_count);
    hooks.add_mid(0x1b5f332,curve_count);
    hooks.add_mid(0x1b5f54d,interpolation_count);
    hooks.add_mid(0x1b5f45e,character);
}
}
