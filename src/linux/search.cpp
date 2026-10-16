#include "linux/native.hpp"
#include "unicode_search.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <atomic>
#include <string>

namespace eu4unicode::linux_native {
namespace {
SafetyHookInline *find_hook=nullptr,*distance_hook=nullptr,*lower_hook=nullptr,*clean_hook=nullptr;
std::atomic<unsigned long long> matches{0};
bool search_normalization(std::uintptr_t caller) noexcept {
    constexpr std::uintptr_t sites[]={0x1cacbc4,0x1cacbcc,0x1cacbf6,0x1cacbfe,
        0x1cacd94,0x1cacd9c,0x1cacef6,0x1cacefe,0x1cad042,0x1cad04c,
        0x1a8bcdb,0x1a8bce3};
    return std::find(std::begin(sites),std::end(sites),caller)!=std::end(sites);
}
void normalize(String* value,SafetyHookInline* original,std::uintptr_t caller) {
    if(!search_normalization(caller)||!valid_utf8(value->view())) {
        original->call<void>(value);return;
    }
    const auto key=latin_search_key(value->view());
    reinterpret_cast<void*(*)(void*,std::size_t,std::size_t,const char*,std::size_t)>(
        0x25a3f30)(value,0,value->size,key.data(),key.size());
}
void lower(String* value) {
    normalize(value,lower_hook,reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
}
void clean(String* value) {
    normalize(value,clean_hook,reinterpret_cast<std::uintptr_t>(__builtin_return_address(0)));
}
int find(const String* name,const String* query,int start) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    if(caller!=0x1a8bcf5&&caller!=0x1cad0d1&&caller!=0x1cad166)
        return find_hook->call<int>(name,query,start);
    if(start<0||static_cast<std::size_t>(start)>name->size) return -1;
    const auto candidate=name->view().substr(start);
    bool found;
    if(caller==0x1a8bcf5) found=display_search_contains(candidate,query->view());
    else {
        // Goto's Find branch is the strict filter or its preferred substring
        // tier. Tolerant pinyin belongs in the following native distance tier;
        // treating it as a direct hit bypasses ranking and exact-only mode.
        const auto quality=display_search_distance(candidate,query->view());
        found=quality>=0&&quality<=1;
    }
    if(found) matches.fetch_add(1,std::memory_order_relaxed);
    // Verified consumers use only found/not-found, never the returned offset.
    return found?0:-1;
}
int distance(const String* name,const String* query) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    if(caller!=0x1cad1e1&&caller!=0x1cad206)
        return distance_hook->call<int>(name,query);
    const auto matched=display_search_distance(name->view(),query->view());
    if(matched>=0) return matched;
    const auto keys=display_search_latin_keys(name->view(),query->view());
    const auto name_key=borrowed_string(keys.first),query_key=borrowed_string(keys.second);
    return distance_hook->call<int>(&name_key,&query_key);
}
}
void prepare_search(Hooks& hooks) {
    verify_site(0x25a3f30);
    find_hook=&hooks.add_inline(0x254c402,reinterpret_cast<void*>(find));
    distance_hook=&hooks.add_inline(0x2557227,reinterpret_cast<void*>(distance));
    lower_hook=&hooks.add_inline(0x254ceb2,reinterpret_cast<void*>(lower));
    clean_hook=&hooks.add_inline(0x254cd44,reinterpret_cast<void*>(clean));
    log("Chinese and pinyin country/province search prepared");
}
}
