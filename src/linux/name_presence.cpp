#include "linux/native.hpp"
#include "name_policy.hpp"
#include "unicode_text.hpp"
#include <atomic>
#include <cstring>
#include <limits>
#include <string>

namespace eu4unicode::linux_native {
namespace {
SafetyHookInline* conversion_hook=nullptr;
std::atomic<unsigned long long> names{0},presence{0};
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
        // Keep all allocation, assignment and destruction with the native owner.
        reinterpret_cast<void*(*)(void*,std::size_t,std::size_t,const char*,std::size_t)>(
            0x25a3f30)(target,0,target->size,result.data(),result.size());
        context.rax=context.rdi;
        context.rip=next; // Skip only the verified split-surname append call.
        names.fetch_add(1,std::memory_order_relaxed);
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
}
void report_name_presence_counts() noexcept {
    log("Native generated names formatted=%llu; Steam UTF-8 presence preserved=%llu",names.load(),presence.load());
}
}
