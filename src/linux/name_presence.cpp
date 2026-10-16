#include "linux/native.hpp"
#include "name_policy.hpp"
#include "unicode_text.hpp"
#include "picosha2.h"
#include <atomic>
#include <cstring>
#include <limits>
#include <string>

namespace eu4unicode::linux_native {
namespace {
SafetyHookInline* conversion_hook=nullptr;
std::atomic<unsigned long long> names{0},presence{0};
std::atomic<unsigned> presence_payloads{0};
struct Payload {std::string key,sha256;std::size_t bytes=0;bool valid=false,active=false;};
thread_local Payload payload;
void before_presence(safetyhook::Context& context) noexcept {
    payload={};
    if(presence_payloads.load()>=32) return;
    try {
        const auto key=reinterpret_cast<const char*>(context.rsi),value=reinterpret_cast<const char*>(context.rdx);
        const auto key_size=strnlen(key,64),size=strnlen(value,4097);
        if(key_size>=64||size>4096) return;
        payload.key.assign(key,key_size);payload.bytes=size;payload.valid=valid_utf8({value,size});
        payload.sha256=picosha2::hash256_hex_string(std::string(value,size));payload.active=true;
    } catch(...) {payload={};}
}
void after_presence(safetyhook::Context& context) noexcept {
    if(!payload.active) return;
    if(presence_payloads.fetch_add(1)<32)
        log("Steam native presence key=%s bytes=%zu valid-utf8=%d accepted=%d sha256=%s",
            payload.key.c_str(),payload.bytes,payload.valid,static_cast<unsigned char>(context.rax)!=0,payload.sha256.c_str());
    payload={};
}
bool policy_for(const void* culture,bool marked,NamePolicy& policy) {
    const auto key=culture?reinterpret_cast<const String*(*)(const void*)>(0xf4d1c8)(culture)->view():std::string_view{};
    bool configured=culture_name_policy(key,policy);
    if(marked&&!configured) policy.separator=NameSeparator::none;
    if(!key.empty()) {
        const auto setting="EU4_UNICODE_NAME_"+std::string(key);
        const auto value=reinterpret_cast<const char*(*)(const char*)>(0x23677f0)(setting.c_str());
        if(value) {
            const auto length=strnlen(value,64);
            if(length<64&&parse_name_policy({value,length},policy)) configured=true;
        }
    }
    if(marked) policy.order=NameOrder::surname_first;
    return configured||marked;
}
void generated(safetyhook::Context& context,std::uintptr_t next) noexcept {
    try {
        auto target=reinterpret_cast<String*>(context.rdi);
        auto family=reinterpret_cast<const String*>(context.rsi)->view();
        if(family.empty()) return;
        std::size_t marker=0;
        if(family.substr(0,2)=="\xc2\xbf") marker=2;
        else if(static_cast<unsigned char>(family.front())==0xbf) marker=1;
        const void* culture=nullptr;
        std::memcpy(&culture,reinterpret_cast<void*>(context.rsp+8),sizeof(culture));
        NamePolicy policy;
        if(!policy_for(culture,marker!=0,policy)) return;
        auto given=target->view();
        if(!given.empty()&&given.back()==' ') given.remove_suffix(1);
        const auto result=format_person_name(given,family.substr(marker),policy);
        // Hash borrowed sources before native assignment can reallocate them.
        const bool sample=names.load(std::memory_order_relaxed)<12;
        const auto given_sha=sample?picosha2::hash256_hex_string(std::string(given)):std::string{};
        const auto family_sha=sample?picosha2::hash256_hex_string(std::string(family.substr(marker))):std::string{};
        // Keep all allocation, assignment and destruction with the native owner.
        reinterpret_cast<void*(*)(void*,std::size_t,std::size_t,const char*,std::size_t)>(
            0x25a3f30)(target,0,target->size,result.data(),result.size());
        context.rax=context.rdi;
        context.rip=next; // Skip only the verified split-surname append call.
        if(names.fetch_add(1,std::memory_order_relaxed)<12&&sample) {
            const auto key=culture?reinterpret_cast<const String*(*)(const void*)>(0xf4d1c8)(culture)->view():std::string_view{};
            log("Native generated name culture=%.*s surname-first=%d given-sha256=%s family-sha256=%s result-sha256=%s bytes=%zu",
                static_cast<int>(key.size()),key.data(),policy.order==NameOrder::surname_first,
                given_sha.c_str(),family_sha.c_str(),
                picosha2::hash256_hex_string(result).c_str(),result.size());
        }
    } catch(...) { log("Generated name policy failed; retaining native append"); }
}
void explicit_surname(safetyhook::Context& context) { generated(context,0xffc162); }
void table_surname(safetyhook::Context& context) { generated(context,0xffc1aa); }
void dynasty_surname(safetyhook::Context& context) { generated(context,0xffc205); }
String* convert(String* target,const String* source) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    if(caller!=0x16c1e16||source->size>std::numeric_limits<int>::max()||!valid_utf8(source->view()))
        return conversion_hook->call<String*>(target,source);
    // This native function returns a freshly constructed CString through sret.
    // Avoid reconverting already UTF-8 data and retain the engine's ownership.
    reinterpret_cast<void(*)(String*,const char*,int)>(0x254b1ea)(target,source->bytes,static_cast<int>(source->size));
    presence.fetch_add(1,std::memory_order_relaxed);
    return target;
}
}
void prepare_name_presence(Hooks& hooks) {
    for(const auto site:{0xf4d1c8,0x23677f0,0x25a3f30,0x254b1ea,0x16c1e11}) verify_site(site);
    hooks.add_mid(0xffc15d,explicit_surname);
    hooks.add_mid(0xffc1a5,table_surname);
    hooks.add_mid(0xffc200,dynasty_surname);
    conversion_hook=&hooks.add_inline(0x254dabc,reinterpret_cast<void*>(convert));
    // Observe the actual native Steam handoff and its return value. Account
    // text is never logged; hashes bind game-side payload bytes to evidence.
    hooks.add_mid(0x16c1e49,before_presence);
    hooks.add_mid(0x16c1e4f,after_presence);
}
void report_name_presence_counts() noexcept {
    log("Native generated names formatted=%llu; Steam UTF-8 presence preserved=%llu",names.load(),presence.load());
}
}
