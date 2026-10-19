#include "linux/native.hpp"
#include "localized_format.hpp"
#include <atomic>
#include <cstring>
namespace eu4unicode::linux_native {
namespace {
SafetyHookInline *date_hook=nullptr,*long_date_hook=nullptr,*month_year_hook=nullptr,*battle_hook=nullptr;
std::atomic<unsigned long long> dates{0},titles{0};
void assign(String* result,const std::string& replacement) {
    if(replacement.empty()) return;
    reinterpret_cast<void*(*)(void*,std::size_t,std::size_t,const char*,std::size_t)>(0x25a3f30)(
        result,0,result->size,replacement.data(),replacement.size());
}
String* format_date(String* result) {
    const auto value=result->view();
    const auto first=value.find(' '),last=value.rfind(' ');
    if(first!=value.npos&&last>first) {
        const auto replacement=chinese_date(value.substr(0,first),value.substr(first+1,last-first-1),value.substr(last+1));
        if(!replacement.empty()) {assign(result,replacement);++dates;}
    }
    return result;
}
String* date(String* result,const void* value,const String* format) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    date_hook->call<String*>(result,value,format);
    if(caller!=0x1c6724c) return result;
    try {format_date(result);} catch(...) {log("Localized date formatting failed; retaining native text");}
    return result;
}
String* long_date(String* result,const void* value,bool short_month) {
    long_date_hook->call<String*>(result,value,short_month);
    try {format_date(result);} catch(...) {log("Localized long date formatting failed; retaining native text");}
    return result;
}
String* month_year(String* result,const void* value) {
    month_year_hook->call<String*>(result,value);
    try {
        const auto text=result->view();
        const auto space=text.rfind(' ');
        if(space!=text.npos) {
            const auto replacement=chinese_month_year(text.substr(0,space),text.substr(space+1));
            if(!replacement.empty()) {assign(result,replacement);++dates;}
        }
    } catch(...) {log("Localized month/year formatting failed; retaining native text");}
    return result;
}
String* battle(String* result,const void* combat) {
    battle_hook->call<String*>(result,combat);
    try {
        const void* province=nullptr;
        std::memcpy(&province,static_cast<const char*>(combat)+0x28,sizeof(province));
        const auto place=reinterpret_cast<const String*(*)(const void*)>(0x1612348)(province)->view();
        const auto text=result->view();
        if(text.size()>=place.size()&&text.substr(text.size()-place.size())==place) {
            const auto replacement=chinese_battle_title(text.substr(0,text.size()-place.size()),place);
            if(!replacement.empty()) {assign(result,replacement);++titles;}
        }
    } catch(...) {log("Localized battle title formatting failed; retaining native text");}
    return result;
}
}
void prepare_localized_format(Hooks& hooks) {
    verify_site(0x25a3f30);verify_site(0x1612348);verify_site(0x1c67247);
    date_hook=&hooks.add_inline(0x1fe8dce,reinterpret_cast<void*>(date));
    long_date_hook=&hooks.add_inline(0x137952c,reinterpret_cast<void*>(long_date));
    month_year_hook=&hooks.add_inline(0x1379fd8,reinterpret_cast<void*>(month_year));
    battle_hook=&hooks.add_inline(0xf05b32,reinterpret_cast<void*>(battle));
}
void report_localized_format_counts() noexcept {log("Localized dates=%llu battle-titles=%llu",dates.load(),titles.load());}
}
