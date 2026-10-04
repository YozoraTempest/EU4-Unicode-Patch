#include "native_search.hpp"
#include "unicode_search.hpp"
#include <cstring>
#include <string>

namespace eu4unicode {
bool display_find_caller(std::uintptr_t caller) noexcept {
    return caller==0xefc394||caller==0x1141feb||caller==0x11420e2;
}
bool province_distance_caller(std::uintptr_t caller) noexcept {
    return caller==0x1142192||caller==0x11421b2;
}
std::uint64_t find_display_name(std::uintptr_t caller,const char* name,std::uint64_t length,
    std::uint64_t start,const char* query,std::uint64_t query_length,NativeFindText original) {
    // These callers test only found/not-found. All offset consumers stay native.
    if(!display_find_caller(caller)) return original(name,length,start,query,query_length);
    if(start>length) return UINT64_MAX;
    return display_search_contains(std::string_view(name,static_cast<std::size_t>(length)).substr(static_cast<std::size_t>(start)),
        std::string_view(query,static_cast<std::size_t>(query_length)))?0:UINT64_MAX;
}
namespace {
EngineString borrowed_string(const std::string& text) {
    EngineString result{};
    result.size=text.size();
    if(text.size()<16) {
        result.capacity=15;
        std::memcpy(result.storage.inline_bytes,text.c_str(),text.size()+1);
    } else {
        result.capacity=text.size();result.storage.pointer=text.c_str();
    }
    return result;
}
}
std::int64_t province_search_distance(std::uintptr_t caller,const EngineString* name,
    const EngineString* query,NativeSearchDistance original) {
    if(!province_distance_caller(caller)) return original(name,query);
    const auto name_view=std::string_view(name->data(),static_cast<std::size_t>(name->size));
    const auto query_view=std::string_view(query->data(),static_cast<std::size_t>(query->size));
    const auto matching=display_search_distance(name_view,query_view);
    if(matching>=0) return matching;
    // The observed routine reads both strings without changing their storage.
    // Preserve its ordinary Latin fuzzy-search distance on normalized copies.
    const auto normalized=display_search_latin_keys(name_view,query_view);
    const auto native_name=borrowed_string(normalized.first),native_query=borrowed_string(normalized.second);
    return original(&native_name,&native_query);
}
}
