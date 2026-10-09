#include "native_localized_format.hpp"
#include "localized_format.hpp"
#include <intrin.h>
#include <cstddef>

namespace eu4unicode {
NativeDateFormat original_localized_date=nullptr;
NativeDateConcat original_localized_date_concat=nullptr;
NativeBattleTitle original_localized_battle_title=nullptr;
LocalizedAssign localized_assign=nullptr;
namespace {
std::uintptr_t image_base=0;
std::string_view text(const EngineString* value) { return {value->data(),static_cast<std::size_t>(value->size)}; }
void assign(EngineString* result,const std::string& value) {
    if(!value.empty()) localized_assign(result,value.data(),value.size());
}
}
void configure_localized_format(const void* image) noexcept { image_base=reinterpret_cast<std::uintptr_t>(image); }
EngineString* format_localized_date(const void* date,EngineString* result,const EngineString* format) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-image_base;
    auto* output=original_localized_date(date,result,format);
    if(caller!=0x143225b) return output;
    try {
        const auto value=text(output);
        const auto first=value.find(' '),last=value.rfind(' ');
        if(first!=std::string_view::npos&&last>first)
            assign(output,chinese_date(value.substr(0,first),value.substr(first+1,last-first-1),value.substr(last+1)));
    } catch(...) { /* Keep the native result if formatting cannot allocate. */ }
    return output;
}
EngineString* concat_localized_date(EngineString* result,const EngineString* month,const EngineString* year) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-image_base;
    std::string replacement;
    if(caller==0x6e5cf4||caller==0x6e61be) {
        try { replacement=chinese_month_year(text(month),text(year)); } catch(...) {}
    }
    auto* output=original_localized_date_concat(result,month,year);
    assign(output,replacement);
    return output;
}
EngineString* format_localized_battle_title(const void* battle,EngineString* result) {
    auto* output=original_localized_battle_title(battle,result);
    try {
        // CBattle's native title builder reads the province name through these
        // same fields (23aa1c..23aa24), for both battle and siege titles.
        const auto* context=*reinterpret_cast<const std::byte* const*>(static_cast<const std::byte*>(battle)+0x30);
        const auto* province=*reinterpret_cast<const std::byte* const*>(context+0x28);
        const auto place=text(reinterpret_cast<const EngineString*>(province+0x10));
        const auto value=text(output);
        if(value.size()>=place.size()&&value.substr(value.size()-place.size())==place)
            assign(output,chinese_battle_title(value.substr(0,value.size()-place.size()),place));
    } catch(...) {}
    return output;
}
}
