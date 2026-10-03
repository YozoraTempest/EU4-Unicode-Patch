#include <windows.h>
#include <bcrypt.h>
#include <MinHook.h>
#include "unicode_text.hpp"
#include "unicode_services.hpp"
#include "unicode_editor.hpp"
#include "unicode_search.hpp"
#include "glyph_registry.hpp"
#include <array>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <intrin.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
HANDLE log_file=INVALID_HANDLE_VALUE;
std::byte* image=nullptr;
std::atomic<bool> patch_enabled{false};
thread_local std::uint32_t last_slot=0;
thread_local std::uint32_t button_extra=0;
thread_local std::uint32_t last_scalar_bytes=1;
using BreakPositions=std::vector<std::size_t>;
thread_local std::shared_ptr<const BreakPositions> active_line_breaks;
thread_local std::unordered_map<std::string,std::shared_ptr<const BreakPositions>> line_break_cache;
thread_local std::size_t line_cache_bytes=0;
void log(const char* message) {
    if(log_file==INVALID_HANDLE_VALUE) return;
    DWORD written=0;
    WriteFile(log_file,message,static_cast<DWORD>(std::strlen(message)),&written,nullptr);
    WriteFile(log_file,"\r\n",2,&written,nullptr);
    FlushFileBuffers(log_file);
}
struct EngineString {
    union { char small[16]; const char* pointer; } storage;
    std::uint64_t size;
    std::uint64_t capacity;
    const char* data() const { return capacity<16?storage.small:storage.pointer; }
};
static_assert(sizeof(EngineString)==32);
using Transliterate=void(*)(EngineString*);
Transliterate original_transliterate=nullptr;
void transliterate_save_path(EngineString* text) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-
        reinterpret_cast<std::uintptr_t>(image);
    const auto value=std::string_view(text->data(),static_cast<std::size_t>(text->size));
    // Preserve only the observed save-games filesystem path. The virtual
    // filesystem still performs its ordinary illegal-path validation.
    if(caller==0x5ca24b && value.substr(0,11)=="save games/" && eu4unicode::valid_utf8(value)) return;
    original_transliterate(text);
}
using FindText=std::uint64_t(*)(const char*,std::uint64_t,std::uint64_t,const char*,std::uint64_t);
FindText original_find_text=nullptr;
std::uint64_t find_country_name(const char* name,std::uint64_t length,std::uint64_t start,
                              const char* query,std::uint64_t query_length) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-
        reinterpret_cast<std::uintptr_t>(image);
    // This country-list caller tests only found/not-found, never the offset.
    // Other string-search callers retain the native byte-offset contract.
    if(caller!=0xefc394) return original_find_text(name,length,start,query,query_length);
    try {
        return start<=length&&eu4unicode::country_search_contains(
            std::string_view(name,static_cast<std::size_t>(length)).substr(static_cast<std::size_t>(start)),
            std::string_view(query,static_cast<std::size_t>(query_length)))?0:UINT64_MAX;
    } catch(...) { log("Unicode country-name search failed; candidate excluded."); return UINT64_MAX; }
}
struct LoadContext { int line; bool replace; char padding[11]; void* collection; };
static_assert(offsetof(LoadContext,collection)==16);
using RegisterText=void(*)(void*,const char*,const char*,int,int,bool);
RegisterText register_text=nullptr;
using RepeatText=char*(*)(EngineString*,std::uint64_t,unsigned char);
RepeatText repeat_text=nullptr;
using AppendText=void*(*)(EngineString*,const char*,std::uint64_t);
AppendText append_text=nullptr;
struct KeyEvent { std::uint32_t key,unused,modifiers; };
using AssignText=EngineString*(*)(EngineString*,const char*,std::uint64_t);
AssignText original_assign_text=nullptr;
using FilterText=void(*)(EngineString*,const EngineString*);
FilterText original_filter_text=nullptr;
void filter_editor_text(EngineString* text,const EngineString* blacklist) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-
        reinterpret_cast<std::uintptr_t>(image);
    const auto value=std::string_view(text->data(),static_cast<std::size_t>(text->size));
    if(caller!=0x1536c37 || !eu4unicode::valid_utf8(value)) {
        original_filter_text(text,blacklist); return;
    }
    try {
        const auto filtered=eu4unicode::filter_editor_characters(value,
            std::string_view(blacklist->data(),static_cast<std::size_t>(blacklist->size)));
        auto data=const_cast<char*>(text->data());
        std::memcpy(data,filtered.data(),filtered.size());
        data[filtered.size()]='\0';
        text->size=filtered.size();
    } catch(...) { log("Unicode editor filtering failed; insertion excluded."); text->size=0; const_cast<char*>(text->data())[0]='\0'; }
}
EngineString* assign_editor_prefix(EngineString* target,const char* source,std::uint64_t length) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-
        reinterpret_cast<std::uintptr_t>(image);
    // These three native editor branches assign a byte-limited prefix of a
    // complete NUL-terminated string. All other assignment contracts stay native.
    if(caller==0x153a30b || caller==0x153a71c || caller==0x153a9a0) {
        const auto complete=std::string_view(source,std::strlen(source));
        if(eu4unicode::valid_utf8(complete)) {
            try { length=eu4unicode::grapheme_prefix(complete,static_cast<std::size_t>(length)); }
            catch(...) { log("Unicode editor prefix failed; truncated insertion excluded."); length=0; }
        }
    }
    return original_assign_text(target,source,length);
}
using EditorKey=bool(*)(void*,const KeyEvent*);
EditorKey original_editor_key=nullptr;
bool editor_key(void* widget,const KeyEvent* event) {
    auto base=static_cast<std::byte*>(widget);
    auto text=reinterpret_cast<EngineString*>(base+0x30);
    auto column=reinterpret_cast<std::uint16_t*>(base+0x54);
    const auto row=*reinterpret_cast<std::uint16_t*>(base+0x56);
    const auto selection=*reinterpret_cast<std::uint64_t*>(base+0x80);
    if(event->modifiers || row || selection || *column>text->size)
        return original_editor_key(widget,event);
    const auto value=std::string_view(text->data(),static_cast<std::size_t>(text->size));
    if(!eu4unicode::valid_utf8(value)) return original_editor_key(widget,event);
    eu4unicode::EditPlan plan{};
    try {
        eu4unicode::EditKey key;
        switch(event->key) {
        case 8: key=eu4unicode::EditKey::backspace; break;
        case 127: key=eu4unicode::EditKey::forward_delete; break;
        case 0x40000050: key=eu4unicode::EditKey::left; break;
        case 0x4000004f: key=eu4unicode::EditKey::right; break;
        default: return original_editor_key(widget,event);
        }
        plan=eu4unicode::plan_edit(value,*column,key);
    } catch(...) { log("Unicode editing boundary failed; key ignored."); return true; }
    if(event->key==8 || event->key==127) {
        const auto start=plan.erase_begin,end=plan.erase_end;
        const auto length=end-start;
        if(length<=1) return original_editor_key(widget,event);
        // Remove all but the cluster's first byte before invoking the native
        // one-byte deletion. The engine then emits its ordinary change event
        // only after the complete grapheme has been removed.
        auto data=const_cast<char*>(text->data());
        std::memmove(data+start+1,data+end,static_cast<std::size_t>(text->size)-end+1);
        text->size-=length-1;
        *column=static_cast<std::uint16_t>(event->key==8?start+1:start);
        return original_editor_key(widget,event);
    }
    const auto length=plan.caret>*column?plan.caret-*column:*column-plan.caret;
    if(length<=1) return original_editor_key(widget,event);
    bool handled=false;
    for(std::size_t i=0;i<length;++i) handled=original_editor_key(widget,event)||handled;
    return handled;
}
void import_text(const EngineString* key,const EngineString* value,int version,
                 int /*unused*/,const LoadContext* context) {
    const auto text=std::string_view(value->data(),static_cast<std::size_t>(value->size));
    if(!eu4unicode::valid_utf8(text)) {
        log("Rejected invalid UTF-8 localization value.");
        return;
    }
    register_text(context->collection,key->data(),value->data(),context->line,version,context->replace);
    if(std::strcmp(key->data(),"FE_SINGLE_PLAYER")==0) {
        log("Registered FE_SINGLE_PLAYER without single-byte conversion:");
        log(value->data());
    }
}

bool hash_matches(const std::filesystem::path& file) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    DWORD object_size=0,returned=0;
    BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_size),sizeof(object_size),&returned,0);
    std::vector<UCHAR> object(object_size);
    bool good=BCryptCreateHash(algorithm,&hash,object.data(),object_size,nullptr,0,0)>=0;
    std::ifstream stream(file,std::ios::binary);
    std::array<char,65536> buffer{};
    if(!stream) good=false;
    while(good && stream) {
        stream.read(buffer.data(),buffer.size());
        good=BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),static_cast<ULONG>(stream.gcount()),0)>=0;
    }
    std::array<UCHAR,32> digest{};
    if(good) good=BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);
    constexpr UCHAR expected[]={0x9a,0xd3,0xef,0xe1,0xaf,0x16,0x9f,0x40,0xee,0x57,0x7f,0x9d,0xae,0x5d,0xeb,0xbc,
        0x87,0xaf,0x6f,0xb8,0xb5,0x45,0x0f,0xb3,0x45,0xeb,0xf1,0x10,0xdc,0x4d,0x77,0x1a};
    return good && std::memcmp(digest.data(),expected,32)==0;
}
struct Site { std::size_t rva; const char* expected; };
std::vector<std::byte> bytes(const char* hex) {
    std::vector<std::byte> result;
    while(*hex) { char pair[]={hex[0],hex[1],0}; result.push_back(static_cast<std::byte>(std::strtoul(pair,nullptr,16))); hex+=2; }
    return result;
}
bool check(const Site& site) {
    const auto expected=bytes(site.expected);
    if(std::memcmp(image+site.rva,expected.data(),expected.size())==0) return true;
    char message[100]; std::snprintf(message,sizeof(message),"Instruction mismatch at RVA %zx; patch refused.",site.rva); log(message);
    return false;
}
bool write(std::size_t rva,const void* data,std::size_t size) {
    DWORD previous=0,unused=0;
    if(!VirtualProtect(image+rva,size,PAGE_EXECUTE_READWRITE,&previous)) return false;
    std::memcpy(image+rva,data,size);
    FlushInstructionCache(GetCurrentProcess(),image+rva,size);
    return VirtualProtect(image+rva,size,previous,&unused)!=0;
}
}

extern "C" {
std::uintptr_t g_main_draw_return,g_main_copy_return,g_main_measure_return;
std::uintptr_t g_bitmap_measure_return,g_bitmap_split_return,g_copy_buffer;
std::uintptr_t g_heap_pointer,g_heap_alloc,g_heap_return;
std::uintptr_t g_button_copy_return,g_button_measure_return,g_button_draw_return,g_button_loop,g_button_end;
std::uintptr_t g_alternate_measure_return,g_wrap_return,g_wrap_branch;
std::uintptr_t g_main_measure_entry,g_main_format_return,g_main_plain_entry;
std::uintptr_t g_main_icon_copy_return,g_main_icon_draw_return;
std::uintptr_t g_button_format_return,g_button_plain_entry,g_button_draw_format_return,g_button_draw_plain_entry;
std::uintptr_t g_button_icon_copy_return,g_button_icon_draw_return;
std::uintptr_t g_bitmap_format_return,g_bitmap_plain_entry,g_bitmap_icon_end_return;
std::uintptr_t g_map_copy_return,g_map_measure_return,g_map_draw_return,g_map_kern_return;
std::uintptr_t g_map_justify_draw_return,g_map_justify_measure_return,g_map_justify_advance_return;
std::uintptr_t g_map_adjust_copy_return,g_map_adjust_glyph_return,g_map_upper_return,g_map_lower_return;
std::uintptr_t g_map_vertex_count_return;
std::uintptr_t g_map_kern_call;
std::uintptr_t g_input_return;
std::uintptr_t g_text_limit_return;
std::uintptr_t g_font_allocate,g_font_duplicate,g_font_store_return,g_font_initialize,g_font_skip,g_engine_new;
std::uintptr_t g_path_pair_return;
std::uintptr_t g_wide_compare_left_return,g_wide_compare_right_return;
void main_draw_hook(); void main_copy_hook(); void main_measure_hook();
void bitmap_measure_hook(); void bitmap_split_hook();
void heap_zero_hook();
void button_copy_hook(); void button_measure_hook(); void button_draw_hook(); void button_advance_hook();
void alternate_measure_hook(); void main_wrap_hook();
void main_format_hook(); void main_icon_copy_hook(); void main_icon_draw_hook();
void button_format_hook(); void button_draw_format_hook();
void button_icon_copy_hook(); void button_icon_draw_hook();
void bitmap_format_hook(); void bitmap_icon_end_hook();
void map_copy_hook(); void map_measure_hook(); void map_draw_hook(); void map_kern_hook();
void map_justify_draw_hook(); void map_justify_measure_hook(); void map_justify_advance_hook();
void map_adjust_copy_hook(); void map_adjust_glyph_hook(); void map_upper_hook(); void map_lower_hook();
void map_vertex_count_hook();
void input_hook();
void text_limit_hook();
void font_lookup_hook(); void font_store_hook();
void path_pair_hook();
void wide_compare_left_hook(); void wide_compare_right_hook();
void font_allocate_hook();
void* allocate_unicode_glyph(void* const* table,std::uint32_t scalar) noexcept {
    auto record=eu4unicode::allocate_unicode_glyph(table,scalar);
    if(!record) log("Unicode glyph allocation failed; glyph skipped.");
    return record;
}
void* find_supplementary_glyph(void* const* table,std::uint32_t scalar) noexcept {
    return eu4unicode::find_unicode_glyph(table,scalar);
}
void* find_loaded_glyph(void* const* table,std::uint32_t scalar) noexcept {
    return scalar<=0xff?table[scalar]:find_supplementary_glyph(table,scalar);
}
void store_loaded_glyph(void** table,std::uint32_t scalar,void* glyph) noexcept {
    if(scalar<=0xff) {
        table[scalar]=glyph;
        if(scalar==0x41&&!eu4unicode::bind_unicode_font(table))
            log("Unicode font alias binding failed.");
    }
}
std::size_t bounded_text_length(const char* source,std::size_t length) noexcept {
    return eu4unicode::scalar_prefix({source,length},32000);
}
std::uint64_t decode_z(const char* text) noexcept {
    std::size_t length=0;
    while(length<4 && text[length]) ++length;
    const auto scalar=eu4unicode::decode({text,length});
    // Engine-owned compiled literals still include Windows-1252 bytes. This
    // adapter is confined to glyph lookup; localization input remains strict.
    const auto slot=scalar.valid?eu4unicode::bitmap_slot(scalar.value):static_cast<unsigned char>(*text);
    const auto consumed=scalar.bytes?scalar.bytes:1;
    return slot | (static_cast<std::uint64_t>(consumed-1)<<32);
}
std::uint64_t copy_scalar(const char* source,std::size_t remaining,char* destination,std::size_t available) noexcept {
    auto scalar=eu4unicode::decode({source,remaining});
    const auto consumed=scalar.bytes?scalar.bytes:1;
    last_scalar_bytes=static_cast<std::uint32_t>(consumed);
    last_slot=scalar.valid?eu4unicode::bitmap_slot(scalar.value):static_cast<unsigned char>(*source);
    if(consumed<=available) std::memcpy(destination,source,consumed);
    return last_slot | (static_cast<std::uint64_t>(consumed-1)<<32);
}
void prepare_wrap_context(const char* source,std::size_t length) noexcept {
    active_line_breaks.reset();
    const auto value=std::string_view(source,bounded_text_length(source,length));
    if(!eu4unicode::valid_utf8(value)) return;
    try {
        std::string key(value);
        auto found=line_break_cache.find(key);
        if(found!=line_break_cache.end()) { active_line_breaks=found->second; return; }
        auto positions=std::make_shared<const BreakPositions>(eu4unicode::line_boundaries(value));
        const auto cost=value.size()+positions->size()*sizeof(std::size_t);
        if(line_break_cache.size()>=256 || line_cache_bytes+cost>1024*1024) {
            line_break_cache.clear(); line_cache_bytes=0;
        }
        line_cache_bytes+=cost;
        line_break_cache.emplace(std::move(key),positions);
        active_line_breaks=std::move(positions);
    } catch(...) { log("Unicode line boundary preparation failed."); }
}
bool unicode_wrap_before(std::uint32_t last_byte) noexcept {
    if(!active_line_breaks || last_byte+1<last_scalar_bytes) return false;
    const auto offset=last_byte+1-last_scalar_bytes;
    return std::binary_search(active_line_breaks->begin(),active_line_breaks->end(),offset);
}
std::uint64_t previous_slot() noexcept { return last_slot; }
std::uint64_t previous_extra() noexcept { return button_extra; }
char* construct_scalar(EngineString* target,const char* source) {
    const auto packed=decode_z(source);
    button_extra=static_cast<std::uint32_t>(packed>>32);
    const auto size=button_extra+1;
    auto destination=repeat_text(target,size,static_cast<unsigned char>(*source));
    std::memcpy(destination,source,size);
    return destination;
}
std::uint64_t format_scalar(const char* source) noexcept {
    const auto packed=decode_z(source);
    const auto slot=static_cast<std::uint32_t>(packed);
    // Only the engine's actual format introducers advance before its parser.
    // Ordinary Unicode characters proceed to the UTF-8 glyph iterator.
    return (slot==0xa7 || slot==0xa3 || slot==0xa4) ? packed : slot;
}
void reset_button_extra() noexcept { button_extra=0; }
bool append_icon_tail(EngineString* target,const char* source) {
    if(static_cast<unsigned char>(source[0])!=0xc2 || static_cast<unsigned char>(source[1])!=0xa3) return false;
    append_text(target,source+1,1);
    return true;
}
char* construct_map_scalar(EngineString* target,const char* source) {
    const auto packed=decode_z(source);
    const auto size=static_cast<std::uint32_t>(packed>>32)+1;
    auto destination=repeat_text(target,size,static_cast<unsigned char>(*source));
    std::memcpy(destination,source,size);
    return destination;
}
std::uint64_t map_scalar_size(const EngineString* source,std::size_t offset) noexcept {
    if(offset>=source->size) return 1;
    const auto scalar=eu4unicode::decode({source->data()+offset,static_cast<std::size_t>(source->size)-offset});
    return scalar.bytes?scalar.bytes:1;
}
std::uint64_t map_scalar_gaps(const EngineString* source) noexcept {
    auto remaining=std::string_view(source->data(),static_cast<std::size_t>(source->size));
    std::uint64_t count=0;
    while(!remaining.empty()) { const auto scalar=eu4unicode::decode(remaining); remaining.remove_prefix(scalar.bytes); ++count; }
    // A single scalar has no inter-character spacing. Keep the denominator
    // nonzero; its only position is the beginning of the map label path.
    return count>1?count-1:1;
}
void dispatch_utf8(void* window,void* receiver,const char* payload,std::uint32_t event_value) {
    std::size_t length=0;
    while(length<32 && payload[length]) ++length;
    if(length==32 || !eu4unicode::valid_utf8({payload,length})) {
        log("Rejected malformed SDL UTF-8 text input."); return;
    }
    struct NativeTextEvent {
        std::byte prefix[16];
        unsigned char byte;
        std::byte padding[31];
        std::uint64_t reserved;
        std::uint64_t text_kind;
        std::byte reserved_text[16];
        std::uint32_t type;
        std::uint16_t flags;
        std::uint16_t trailing;
    };
    static_assert(offsetof(NativeTextEvent,text_kind)==0x38 && offsetof(NativeTextEvent,type)==0x50);
    static_assert(sizeof(NativeTextEvent)==0x58);
    using WindowEvent=void(*)(void*,int,std::uint32_t,int);
    using TextEvent=void(*)(void*,NativeTextEvent*);
    auto window_vtable=*static_cast<void***>(window);
    auto receiver_vtable=*static_cast<void***>(receiver);
    for(std::size_t index=0;index<length;++index) {
        reinterpret_cast<WindowEvent>(window_vtable[4])(window,0x303,event_value,0);
        NativeTextEvent event{};
        event.byte=static_cast<unsigned char>(payload[index]);
        event.text_kind=3;
        event.type=2;
        reinterpret_cast<TextEvent>(receiver_vtable[3])(receiver,&event);
    }
}
}

namespace {
bool initialize(HMODULE module) {
    wchar_t exe_path[32768]{},dll_path[32768]{};
    GetModuleFileNameW(nullptr,exe_path,32768);
    GetModuleFileNameW(module,dll_path,32768);
    auto exe=std::filesystem::path(exe_path);
    log_file=CreateFileW((std::filesystem::path(dll_path).parent_path()/L"eu4_unicode_probe.log").c_str(),
        GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    log("EU4 UTF-8 research prototype initializing.");
    // This first prototype is intentionally restricted to the isolated fixture.
    if(exe.parent_path().filename()!=L"runtime" || exe.parent_path().parent_path().filename()!=L"private" ||
       exe.parent_path().parent_path().parent_path().filename()!=L"EU4UnicodePatch") {
        log("Refused: executable is outside the isolated research fixture."); return false;
    }
    if(!hash_matches(exe)) { log("Refused: executable hash mismatch."); return false; }
    if(GetModuleHandleW(L"plugin64.dll") || std::filesystem::exists(exe.parent_path()/L"plugins"/L"plugin64.dll")) {
        log("Refused: legacy text patch is present."); return false;
    }
    image=reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    const bool experimental_input=GetPrivateProfileIntW(L"experimental",L"unicode_input",0,
        (std::filesystem::path(dll_path).parent_path()/L"eu4_unicode_probe.ini").c_str())!=0;
    const Site sites[]={
        {0x15989d8,"b8007d0000443bf8440f4df8"},
        {0x1595c9b,"488b85301100004883bcf82001000000"},
        {0x1595cad,"b910000000e81dd64900"},
        {0x1595ceb,"4c8bbd30110000498984ff20010000"},
        {0x16fd650,"48895c240848896c2410488974241848"},
        {0x15995b0,"4c63cf488b55f84c03ca4863ce410fb6014c8d1d08a7e90042880419ffc6"},
        {0x1599728,"410fb601498b8cc62001000048894d004885c9"},
        {0x159a796,"460fb60409f3410f109e680900004b8b94c620010000"},
        {0x159b687,"0fb60407498b8cc6200100004885c9"},
        {0x159ef48,"f3410f10b6480800000fb604024d8b3cc64d85ff"},
        {0x1595c86,"81ffff000000"}, {0x10b2a66,"b9883d0000"},
        {0x1b24a59,"ba883d0000"}, {0x10999f9,"ba883d0000"},
        {0x16c2cb7,"4181fe00000001"}, {0x1a683ae,"488b0d9b548d004c8bc333d2ff15b0e10f004885c0"},
        {0x1596858,"440fb60418ba01000000488d4c2448e8b49aaffe90"},
        {0x1597071,"458bce410fb604014c8b1cc14d85db"},
        {0x15983a1,"418bcff3440f109a480800000fb604014c8b04c24c894558"},
        {0x15974cb,"41ffc6443b75d80f8c59f3ffff448bbdc8210000"},
        {0x159b91a,"0fb6142b498d8f200100004c8b1cd14d85db"},
        {0x15997a9,"66837906000f85130100008d041b660f6ec8"}
        ,{0x159a240,"4e8d0409410fb6003ca77573"}
        ,{0x15996b1,"c68415d001000000498b06"}
        ,{0x159a45f,"c6840dd001000000498b06"}
        ,{0x15968e1,"488d45c84983ff10490f43c4803c03a7"}
        ,{0x1596e83,"c68415a000000000488b07"}
        ,{0x1598110,"c68415a000000000488b07"}
        ,{0x1597d7d,"488d45a04983fc10490f43c5418bcf803c08a7488d45a0"}
        ,{0x159b539,"4c8b4b18488bcb4983f9107203488b0b803c0fa7"}
        ,{0x159b61e,"498b06488d542420c6440c2000"}
        ,{0x159db79,"450fb60407ba01000000488d4c2458"}
        ,{0x159dc03,"410fb604074d8b9cc1200100004d85db"}
        ,{0x159df89,"f30f115da0410fb60401498b14c74885d2"}
        ,{0x159e436,"488d4c24784883fb10480f43ce440fb60408"}
        ,{0xfd3f40,"0fb60401888508080000f3440f10a2480800004c8b34c24d85f6"}
        ,{0xfd4144,"660f6ef60f5bf6488b8568010000ffc8660f6ec8"}
        ,{0xfd53c4,"ffc689b5e807000048ffc148898d18010000"}
        ,{0xfd6680,"488d85900000004983fd10480f43c60fb60418884500"}
        ,{0xfd6bc0,"488d85900000004983fd10480f43c60fb60408498b14c6"}
        ,{0xfd7330,"488d43104983f9107204488b43100fb60401498b94c420010000"}
        ,{0x14ba825,"0fbe0c28488d1c28e836065900ffc788038bc7"}
        ,{0x1550425,"0fbe0c28488d1c28e80aaa4f00ffc788038bc7"}
        ,{0x1569f91,"8b45bc32db3c8073050fb6d8eb12"}
        ,{0x15366c0,"48895c240848896c24184889742420574883ec40"}
        ,{0x95110,"48895c241048896c2418565741574883ec20"}
        ,{0x153a306,"e805aeb5fe"}
        ,{0x153a717,"e8f4a9b5fe"}
        ,{0x153a99b,"e870a7b5fe"}
        ,{0xb19590,"4053565741544883ec48"}
        ,{0x1536c32,"e859295eff"}
        ,{0x1174e95,"e8e6025900"}
        ,{0x13b9567,"e814bc3400"}
        ,{0x117519e,"e8ddff5800"}
        ,{0x1175c2c,"e84ff55800"}
        ,{0x1705180,"8b411085c00f840c010000"}
        ,{0x19fc097,"8bca4983c302c1e10a0bc885c97417"}
        ,{0x19fbc23,"c1e10a0bc8eb05b93f0000004c8bfa"}
        ,{0x19fbca2,"c1e10a0bc8eb05b93f0000004c8bea"}
        ,{0xefc33b,"e8b0406500"}
        ,{0xefc344,"e8278a8000"}
        ,{0xf16156,"e895a26300"}
        ,{0xf1615e,"e80dec7e00"}
        ,{0x17061a0,"48895c240848896c24104889742418"}
        ,{0xefc38f,"e80c9e800083f8ff"}
    };
    for(const auto& site:sites) if(!check(site)) return false;
    auto address=[](std::size_t rva){ return reinterpret_cast<std::uintptr_t>(image+rva); };
    g_main_draw_return=address(0x159a7ac);
    g_main_copy_return=address(0x15995ce);
    g_main_measure_return=address(0x159973b);
    g_bitmap_measure_return=address(0x159b696);
    g_bitmap_split_return=address(0x159ef5c);
    g_copy_buffer=address(0x2433cd0);
    g_heap_pointer=address(0x233d850);
    g_heap_alloc=address(0x1b66570);
    g_heap_return=address(0x1a683c3);
    g_button_copy_return=address(0x159686c);
    g_button_measure_return=address(0x1597080);
    g_button_draw_return=address(0x15983b9);
    g_button_loop=address(0x1596831);
    g_button_end=address(0x15974df);
    g_alternate_measure_return=address(0x159b92c);
    g_wrap_return=address(0x15997bb);
    g_wrap_branch=address(0x15998c7);
    g_main_measure_entry=address(0x1599728);
    g_main_format_return=address(0x159a24a);
    g_main_plain_entry=address(0x159a791);
    g_main_icon_copy_return=address(0x15996b9);
    g_main_icon_draw_return=address(0x159a467);
    g_button_format_return=address(0x15968f1);
    g_button_plain_entry=address(0x1597059);
    g_button_icon_copy_return=address(0x1596e8b);
    g_button_icon_draw_return=address(0x1598118);
    g_button_draw_format_return=address(0x1597d94);
    g_button_draw_plain_entry=address(0x159838a);
    g_bitmap_format_return=address(0x159b54d);
    g_bitmap_plain_entry=address(0x159b677);
    g_bitmap_icon_end_return=address(0x159b62b);
    g_map_copy_return=address(0x159db8d);
    g_map_measure_return=address(0x159dc13);
    g_map_draw_return=address(0x159df9a);
    g_map_kern_return=address(0x159e45e);
    g_map_kern_call=address(0x15943d0);
    g_map_justify_draw_return=address(0xfd3f5a);
    g_map_justify_measure_return=address(0xfd4158);
    g_map_justify_advance_return=address(0xfd53d6);
    g_map_adjust_copy_return=address(0xfd66ab);
    g_map_adjust_glyph_return=address(0xfd6bd7);
    g_map_vertex_count_return=address(0xfd734a);
    g_map_upper_return=address(0x14ba838);
    g_map_lower_return=address(0x1550438);
    g_input_return=address(0x156a22a);
    g_text_limit_return=address(0x15989e4);
    g_font_allocate=address(0x1595cad);
    g_font_duplicate=address(0x1595d07);
    g_font_store_return=address(0x1595cfa);
    g_font_initialize=address(0x1595cb7);
    g_font_skip=address(0x1595f01);
    g_engine_new=address(0x1a332d4);
    g_path_pair_return=address(0x19fc0a6);
    g_wide_compare_left_return=address(0x19fbc2f);
    g_wide_compare_right_return=address(0x19fbcae);
    repeat_text=reinterpret_cast<RepeatText>(address(0x90320));
    append_text=reinterpret_cast<AppendText>(address(0x932f0));
    register_text=reinterpret_cast<RegisterText>(address(0x16fa8d0));
    if(MH_Initialize()!=MH_OK) { log("MinHook initialization failed."); return false; }
    struct Hook { std::size_t rva; void* callback; };
    const Hook hooks[]={ {0x16fd650,reinterpret_cast<void*>(import_text)},
        {0x15995b0,reinterpret_cast<void*>(main_copy_hook)}, {0x1599728,reinterpret_cast<void*>(main_measure_hook)},
        {0x159a796,reinterpret_cast<void*>(main_draw_hook)}, {0x159b687,reinterpret_cast<void*>(bitmap_measure_hook)},
        {0x159ef48,reinterpret_cast<void*>(bitmap_split_hook)},
        {0x1a683ae,reinterpret_cast<void*>(heap_zero_hook)},
        {0x1596858,reinterpret_cast<void*>(button_copy_hook)},
        {0x1597071,reinterpret_cast<void*>(button_measure_hook)},
        {0x15983a1,reinterpret_cast<void*>(button_draw_hook)},
        {0x15974cb,reinterpret_cast<void*>(button_advance_hook)},
        {0x159b91a,reinterpret_cast<void*>(alternate_measure_hook)},
        {0x15997a9,reinterpret_cast<void*>(main_wrap_hook)},
        {0x159a240,reinterpret_cast<void*>(main_format_hook)},
        {0x15996b1,reinterpret_cast<void*>(main_icon_copy_hook)},
        {0x159a45f,reinterpret_cast<void*>(main_icon_draw_hook)},
        {0x15968e1,reinterpret_cast<void*>(button_format_hook)},
        {0x1596e83,reinterpret_cast<void*>(button_icon_copy_hook)},
        {0x1598110,reinterpret_cast<void*>(button_icon_draw_hook)},
        {0x1597d7d,reinterpret_cast<void*>(button_draw_format_hook)},
        {0x159b539,reinterpret_cast<void*>(bitmap_format_hook)},
        {0x159b61e,reinterpret_cast<void*>(bitmap_icon_end_hook)},
        {0x159db79,reinterpret_cast<void*>(map_copy_hook)},
        {0x159dc03,reinterpret_cast<void*>(map_measure_hook)},
        {0x159df89,reinterpret_cast<void*>(map_draw_hook)},
        {0x159e436,reinterpret_cast<void*>(map_kern_hook)},
        {0xfd3f40,reinterpret_cast<void*>(map_justify_draw_hook)},
        {0xfd4144,reinterpret_cast<void*>(map_justify_measure_hook)},
        {0xfd53c4,reinterpret_cast<void*>(map_justify_advance_hook)},
        {0xfd6680,reinterpret_cast<void*>(map_adjust_copy_hook)},
        {0xfd6bc0,reinterpret_cast<void*>(map_adjust_glyph_hook)},
        {0xfd7330,reinterpret_cast<void*>(map_vertex_count_hook)},
        {0x14ba825,reinterpret_cast<void*>(map_upper_hook)},
        {0x1550425,reinterpret_cast<void*>(map_lower_hook)},
        {0x15989d8,reinterpret_cast<void*>(text_limit_hook)},
        {0x1595c9b,reinterpret_cast<void*>(font_lookup_hook)},
        {0x1595cad,reinterpret_cast<void*>(font_allocate_hook)},
        {0x1595ceb,reinterpret_cast<void*>(font_store_hook)},
        {0x19fc097,reinterpret_cast<void*>(path_pair_hook)},
        {0x19fbc23,reinterpret_cast<void*>(wide_compare_left_hook)},
        {0x19fbca2,reinterpret_cast<void*>(wide_compare_right_hook)} };
    for(const auto& hook:hooks) {
        if(MH_CreateHook(image+hook.rva,hook.callback,nullptr)!=MH_OK) {
            log("Hook creation failed; no hooks enabled."); MH_Uninitialize(); return false;
        }
    }
    if(MH_CreateHook(image+0x1705180,reinterpret_cast<void*>(transliterate_save_path),
        reinterpret_cast<void**>(&original_transliterate))!=MH_OK) {
        log("Save path hook creation failed; no hooks enabled."); MH_Uninitialize(); return false;
    }
    if(MH_CreateHook(image+0x17061a0,reinterpret_cast<void*>(find_country_name),
        reinterpret_cast<void**>(&original_find_text))!=MH_OK) {
        log("Country search hook creation failed; no hooks enabled."); MH_Uninitialize(); return false;
    }
    if(experimental_input) {
        if(MH_CreateHook(image+0x1569f91,reinterpret_cast<void*>(input_hook),nullptr)!=MH_OK ||
           MH_CreateHook(image+0x15366c0,reinterpret_cast<void*>(editor_key),
             reinterpret_cast<void**>(&original_editor_key))!=MH_OK ||
           MH_CreateHook(image+0x95110,reinterpret_cast<void*>(assign_editor_prefix),
             reinterpret_cast<void**>(&original_assign_text))!=MH_OK ||
           MH_CreateHook(image+0xb19590,reinterpret_cast<void*>(filter_editor_text),
             reinterpret_cast<void**>(&original_filter_text))!=MH_OK) {
            log("Input hook creation failed; no hooks enabled."); MH_Uninitialize(); return false;
        }
        log("Experimental UTF-8 input and single-line grapheme editing enabled.");
    }
    struct DataPatch { std::size_t rva; std::vector<std::byte> before,after; };
    // Allocate all patch/rollback buffers before modifying any instruction.
    const DataPatch constants[]={ {0x1595c88,bytes("ff000000"),bytes("ffff1000")},
        {0x16c2cba,bytes("00000001"),bytes("00000004")},
        // Save-name builders and save/load selection call the CP1252 transliterator.
        // Skip only those calls: their strings already contain UTF-8. The
        // later filename-character validation and other callers stay native.
        {0x1174e95,bytes("e8e6025900"),bytes("9090909090")},
        {0x13b9567,bytes("e814bc3400"),bytes("9090909090")},
        {0x117519e,bytes("e8ddff5800"),bytes("9090909090")},
        {0x1175c2c,bytes("e84ff55800"),bytes("9090909090")},
        // Keep both the original query and localized candidate in UTF-8;
        // derive display-search keys only in the scoped matching callback.
        {0xefc33b,bytes("e8b0406500"),bytes("9090909090")},
        {0xefc344,bytes("e8278a8000"),bytes("9090909090")},
        {0xf16156,bytes("e895a26300"),bytes("9090909090")},
        {0xf1615e,bytes("e80dec7e00"),bytes("9090909090")} };
    std::size_t applied=0;
    bool constants_ok=true;
    for(const auto& patch:constants) {
        ++applied;
        if(!write(patch.rva,patch.after.data(),patch.after.size())) { constants_ok=false; break; }
    }
    const bool enabled=constants_ok && MH_EnableHook(MH_ALL_HOOKS)==MH_OK;
    if(!enabled) {
        log("Patch activation failed; restoring original instructions and constants.");
        MH_DisableHook(MH_ALL_HOOKS);
        while(applied) {
            const auto& patch=constants[--applied];
            write(patch.rva,patch.before.data(),patch.before.size());
        }
        MH_Uninitialize();
        return false;
    }
    patch_enabled.store(true,std::memory_order_release);
    log("UTF-8 import, UI, format, map and bitmap iterators enabled.");
    return true;
}
}
extern "C" __declspec(dllexport) int Eu4UnicodeProbeEnabled() noexcept {
    return patch_enabled.load(std::memory_order_acquire)?1:0;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        // thread_local is intentionally retained; do not disable thread notifications.
        try { initialize(module); } catch(...) { log("Initialization exception; prototype disabled."); }
    }
    return TRUE;
}
