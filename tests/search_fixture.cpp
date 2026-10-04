#include "native_search.hpp"
#include "unicode_search.hpp"

extern "C" __declspec(dllexport) void SearchFixtureSetOptions(unsigned typo,unsigned fuzzy) noexcept {
    eu4unicode::set_search_options({typo!=0,fuzzy!=0});
}

extern "C" __declspec(dllexport) std::uint64_t SearchFixtureFind(std::uintptr_t caller,
    const char* name,std::uint64_t length,std::uint64_t start,const char* query,
    std::uint64_t query_length,eu4unicode::NativeFindText original) noexcept {
    try { return eu4unicode::find_display_name(caller,name,length,start,query,query_length,original); }
    catch(...) { return UINT64_MAX; }
}
extern "C" __declspec(dllexport) std::int64_t SearchFixtureDistance(std::uintptr_t caller,
    const eu4unicode::EngineString* name,const eu4unicode::EngineString* query,
    eu4unicode::NativeSearchDistance original) noexcept {
    try { return eu4unicode::province_search_distance(caller,name,query,original); }
    catch(...) { return INT32_MAX/2; }
}
