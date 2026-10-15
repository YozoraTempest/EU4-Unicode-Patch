#include "linux/native.hpp"
#include "unicode_text.hpp"
#include <atomic>

namespace eu4unicode::linux_native {
namespace {
std::atomic<unsigned long long> spacing_scalars{0};
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
void spacing_count(safetyhook::Context& context) { count(context,0x1b5de15); }
std::string_view scalar_at(std::string_view text,std::size_t ordinal) {
    while(ordinal--&&!text.empty()) text.remove_prefix(decode(text).bytes);
    return text.empty()?std::string_view{}:text.substr(0,decode(text).bytes);
}
void append_spacing_scalar(safetyhook::Context& context,bool last) noexcept {
    try {
        const auto source=reinterpret_cast<const String*>(context.rbp-0x150)->view();
        if(!valid_utf8(source)) return;
        auto part=scalar_at(source,context.r12);
        if(last) {
            auto rest=source;
            while(!rest.empty()) { part=rest.substr(0,decode(rest).bytes);rest.remove_prefix(part.size()); }
        }
        auto target=reinterpret_cast<String*>(context.r14);
        reinterpret_cast<void*(*)(void*,std::size_t,std::size_t,const char*,std::size_t)>(
            0x25a3f30)(target,target->size,0,part.data(),part.size());
        context.rax=context.r14;context.rip=last?0x1b5de86:0x1b5de44;
        spacing_scalars.fetch_add(1,std::memory_order_relaxed);
    } catch(...) { log("Map label spacing failed; retaining native append"); }
}
void spacing_character(safetyhook::Context& context) { append_spacing_scalar(context,false); }
void spacing_last(safetyhook::Context& context) { append_spacing_scalar(context,true); }
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
    verify_site(0x25a3f30);
    hooks.add_mid(0x1b5f327,label_count);
    hooks.add_mid(0x1b5f332,curve_count);
    hooks.add_mid(0x1b5f54d,interpolation_count);
    hooks.add_mid(0x1b5f45e,character);
    // AddNameArea stretches labels by inserting spaces. Its byte-by-byte
    // loop otherwise separates UTF-8 continuation bytes before rasterization.
    hooks.add_mid(0x1b5de10,spacing_count);
    hooks.add_mid(0x1b5de2a,spacing_character);
    hooks.add_mid(0x1b5de6a,spacing_last);
}
void report_map_counts() noexcept { log("Map label spacing scalars preserved=%llu",spacing_scalars.load()); }
}
