#include "unicode_search.hpp"
#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace eu4unicode {
namespace {
struct Keys { std::string unicode,latin; };
thread_local std::unordered_map<std::string,std::shared_ptr<const Keys>> cache;
thread_local std::size_t cache_bytes=0;

std::uint32_t latin_equivalent(std::uint32_t scalar) {
    // Actual CP1252 equivalences from the native 1.37.5 search preprocessor.
    // Apply them to Unicode scalars, never to UTF-8 continuation bytes.
    if((scalar>=0xc0&&scalar<=0xc6)||(scalar>=0xe0&&scalar<=0xe6)) return 'a';
    if(scalar==0xc7||scalar==0xe7) return 'c';
    if((scalar>=0xc8&&scalar<=0xcb)||(scalar>=0xe8&&scalar<=0xeb)) return 'e';
    if((scalar>=0xcc&&scalar<=0xcf)||(scalar>=0xec&&scalar<=0xef)) return 'i';
    if(scalar==0xd0||scalar==0xf0) return 'd';
    if(scalar==0xd1||scalar==0xf1) return 'n';
    if((scalar>=0xd2&&scalar<=0xd6)||(scalar>=0xf2&&scalar<=0xf6)||scalar==0xd8||scalar==0xf8) return 'o';
    if((scalar>=0xd9&&scalar<=0xdc)||(scalar>=0xf9&&scalar<=0xfc)) return 'u';
    if(scalar==0xdd||scalar==0xfd||scalar==0xff||scalar==0x178) return 'y';
    if(scalar==0xde||scalar==0xfe) return 't';
    if(scalar==0xdf||scalar==0x1e9e) return 's';
    return scalar;
}
std::shared_ptr<const Keys> keys(std::string_view text) {
    const auto owned=std::string(text);
    const auto found=cache.find(owned);
    if(found!=cache.end()) return found->second;
    auto result=std::make_shared<Keys>();
    result->unicode=search_key(text);
    const auto canonical=canonical_text(text);
    auto remaining=std::string_view(canonical);
    std::string latin;
    while(!remaining.empty()) {
        const auto scalar=decode(remaining);
        latin+=encode(latin_equivalent(scalar.value));
        remaining.remove_prefix(scalar.bytes);
    }
    result->latin=search_key(latin);
    const auto bytes=owned.size()+result->unicode.size()+result->latin.size();
    constexpr std::size_t budget=1024*1024;
    if(bytes<=budget) {
        if(cache.size()>=1024||cache_bytes>budget-bytes) { cache.clear(); cache_bytes=0; }
        cache.emplace(owned,result);
        cache_bytes+=bytes;
    }
    return result;
}
}
bool country_search_contains(std::string_view name,std::string_view query) {
    const auto candidate=keys(name),pattern=keys(query);
    return candidate->unicode.find(pattern->unicode)!=std::string::npos||
        candidate->latin.find(pattern->latin)!=std::string::npos;
}
}
