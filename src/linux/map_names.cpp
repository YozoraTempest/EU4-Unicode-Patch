#include "linux/native.hpp"
#include "linux/paragraph.hpp"
#include "unicode_text.hpp"
#include <atomic>
#include <cstring>

namespace eu4unicode::linux_native {
namespace {
std::atomic<unsigned long long> spacing_scalars{0};
std::atomic<unsigned long long> shaped_countries{0},shaped_provinces{0};
SafetyHookInline *area_hook=nullptr,*nudged_hook=nullptr;
thread_local ParagraphScope* country_scope=nullptr;
struct ProvinceScope {
    std::unique_ptr<ParagraphScope> paragraph;
};
thread_local ProvinceScope* province_scope=nullptr;
template<class T> T member(const void* object,std::size_t offset) {
    T result;std::memcpy(&result,static_cast<const char*>(object)+offset,sizeof(result));return result;
}
void area(void* work,const int* provinces,int a,int b,int c,int d,const void* data) {
    ParagraphScope paragraph(member<void*>(work,0x30),nullptr);
    const auto previous=country_scope;country_scope=&paragraph;
    struct Reset {ParagraphScope* previous;~Reset(){country_scope=previous;}} reset{previous};
    area_hook->call<void>(work,provinces,a,b,c,d,data);
}
void nudged(void* collection,void* map,const void* provinces,int count,void* font) {
    ProvinceScope scope;
    const auto previous=province_scope;province_scope=&scope;
    struct Reset {ProvinceScope* previous;~Reset(){province_scope=previous;}} reset{previous};
    nudged_hook->call<void>(collection,map,provinces,count,font);
}
void country_shape(safetyhook::Context& context) noexcept {
    if(!country_scope) return;
    try {
        auto source=reinterpret_cast<String*>(context.rbp-0x110);
        const auto font=member<void*>(reinterpret_cast<void*>(context.r13),0x30);
        const auto prepared=country_scope->prepare(source,32000.f*member<float>(font,0x940),false,false,"map-country");
        if(prepared==source) return;
        // This is AddNameArea's owned scratch CString, not the country name.
        reinterpret_cast<void*(*)(void*,std::size_t,std::size_t,const char*,std::size_t)>(0x25a3f30)(
            source,0,source->size,prepared->bytes,prepared->size);
        ++shaped_countries;
    } catch(...) {report_paragraph_failure("map-country","Native scratch assignment failed");}
}
void country_spacing(safetyhook::Context& context) {
    // Inserting spaces between already shaped clusters breaks joined scripts.
    if(country_scope&&country_scope->active()) context.rip=0x1b5df5f;
}
void province_shape(safetyhook::Context& context) noexcept {
    if(!province_scope) return;
    try {
        // End the previous borrowed invocation before installing this one.
        province_scope->paragraph.reset();
        auto source=reinterpret_cast<const String*>(context.rax);
        auto paragraph=std::make_unique<ParagraphScope>(reinterpret_cast<void*>(context.r12),source);
        const auto prepared=paragraph->prepare(source,32000.f*member<float>(reinterpret_cast<void*>(context.r12),0x940),false,false,"map-province");
        context.rax=reinterpret_cast<std::uintptr_t>(prepared);
        if(paragraph->active()) ++shaped_provinces;
        province_scope->paragraph=std::move(paragraph);
    } catch(...) {report_paragraph_failure("map-province","Native label preparation failed");}
}
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
    verify_site(0x1b5df5f);
    area_hook=&hooks.add_inline(0x1b5d334,reinterpret_cast<void*>(area));
    nudged_hook=&hooks.add_inline(0x1b5fd3a,reinterpret_cast<void*>(nudged));
    hooks.add_mid(0x1b5dcd9,country_shape);
    hooks.add_mid(0x1b5dd9c,country_spacing);
    hooks.add_mid(0x1b5fde2,province_shape);
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
void report_map_counts() noexcept {
    log("Map label spacing scalars preserved=%llu",spacing_scalars.load());
    log("Map shaped labels countries=%llu provinces=%llu",shaped_countries.load(),shaped_provinces.load());
}
}
