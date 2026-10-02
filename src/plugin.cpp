#include <windows.h>
#include <bcrypt.h>
#include <MinHook.h>
#include "unicode_text.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
HANDLE log_file=INVALID_HANDLE_VALUE;
std::byte* image=nullptr;
thread_local std::uint32_t last_slot=0;
thread_local std::uint32_t button_extra=0;
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
struct LoadContext { int line; bool replace; char padding[11]; void* collection; };
static_assert(offsetof(LoadContext,collection)==16);
using RegisterText=void(*)(void*,const char*,const char*,int,int,bool);
RegisterText register_text=nullptr;
using RepeatText=char*(*)(EngineString*,std::uint64_t,unsigned char);
RepeatText repeat_text=nullptr;
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
void main_draw_hook(); void main_copy_hook(); void main_measure_hook();
void bitmap_measure_hook(); void bitmap_split_hook();
void heap_zero_hook();
void button_copy_hook(); void button_measure_hook(); void button_draw_hook(); void button_advance_hook();
void alternate_measure_hook(); void main_wrap_hook();
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
    last_slot=scalar.valid?eu4unicode::bitmap_slot(scalar.value):static_cast<unsigned char>(*source);
    if(consumed<=available) std::memcpy(destination,source,consumed);
    return last_slot | (static_cast<std::uint64_t>(consumed-1)<<32);
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
    const Site sites[]={
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
    repeat_text=reinterpret_cast<RepeatText>(address(0x90320));
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
        {0x15997a9,reinterpret_cast<void*>(main_wrap_hook)} };
    for(const auto& hook:hooks) {
        if(MH_CreateHook(image+hook.rva,hook.callback,nullptr)!=MH_OK) {
            log("Hook creation failed; no hooks enabled."); MH_Uninitialize(); return false;
        }
    }
    struct ConstantPatch { std::size_t rva; std::uint32_t before,after; };
    const ConstantPatch constants[]={ {0x1595c88,0xff,0xffff},
        {0x10b2a67,0x3d88,0x103d88}, {0x1b24a5a,0x3d88,0x103d88},
        {0x10999fa,0x3d88,0x103d88}, {0x16c2cba,0x1000000,0x4000000} };
    std::size_t applied=0;
    bool constants_ok=true;
    for(const auto& patch:constants) {
        ++applied;
        if(!write(patch.rva,&patch.after,4)) { constants_ok=false; break; }
    }
    const bool enabled=constants_ok && MH_EnableHook(MH_ALL_HOOKS)==MH_OK;
    if(!enabled) {
        log("Patch activation failed; restoring original instructions and constants.");
        MH_DisableHook(MH_ALL_HOOKS);
        while(applied) { const auto& patch=constants[--applied]; write(patch.rva,&patch.before,4); }
        MH_Uninitialize();
        return false;
    }
    log("UTF-8 import, main drawing, main measurement and bitmap iterators enabled.");
    return true;
}
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        // thread_local is intentionally retained; do not disable thread notifications.
        try { initialize(module); } catch(...) { log("Initialization exception; prototype disabled."); }
    }
    return TRUE;
}
