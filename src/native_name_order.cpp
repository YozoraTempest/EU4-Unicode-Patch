#include "native_name_order.hpp"
#include "unicode_text.hpp"
#include <icu.h>
#include <algorithm>
#include <iterator>

namespace eu4unicode {
NameAppend native_name_append=nullptr;
NameAssign native_name_assign=nullptr;
NamePolicyLookup native_name_policy_lookup=nullptr;
void(*native_name_log)(const char*)=nullptr;
bool culture_name_policy(std::string_view culture,NamePolicy& policy) noexcept {
    if(culture=="hungarian") {
        policy={NameOrder::surname_first,NameSeparator::space};return true;
    }
    // Explicit culture IDs, not the broad east_asian group (which also contains
    // Tibetan and Altaic cultures). Mods can provide policies for their own IDs.
    constexpr std::string_view compact[]={"chihan","cantonese","jin","wu","chimin","hakka","gan",
        "xiang","sichuanese","jianghuai","xibei","hubei","zhongyuan","shandong_culture",
        "korean","korean_new","japanese","togoku","kyushuan"};
    if(std::find(std::begin(compact),std::end(compact),culture)==std::end(compact)) return false;
    policy={NameOrder::surname_first,NameSeparator::automatic};return true;
}
namespace {
bool whitespace(char c) { return c==' '||c=='\t'||c=='\r'||c=='\n'; }
std::string_view word(std::string_view& value) {
    while(!value.empty()&&whitespace(value.front())) value.remove_prefix(1);
    const auto end=value.find_first_of(" \t\r\n");
    const auto result=value.substr(0,end);
    value.remove_prefix(result.size());return result;
}
bool compact_initial(std::string_view text) {
    // Script affects only the separator. Culture/explicit markers decide order.
    while(!text.empty()) {
        const auto scalar=decode(text);
        if(!scalar.valid) return false;
        text.remove_prefix(scalar.bytes);
        if(!u_isalpha(static_cast<UChar32>(scalar.value))) continue;
        UErrorCode status=U_ZERO_ERROR;
        const auto script=uscript_getScript(static_cast<UChar32>(scalar.value),&status);
        return U_SUCCESS(status)&&(script==USCRIPT_HAN||script==USCRIPT_HIRAGANA||
            script==USCRIPT_KATAKANA||script==USCRIPT_HANGUL);
    }
    return false;
}
bool starts_separator(std::string_view text) {
    return !text.empty()&&(whitespace(text.front())||text.substr(0,2)=="·"||text.substr(0,3)=="・");
}
bool ends_separator(std::string_view text) {
    return !text.empty()&&(whitespace(text.back())||
        (text.size()>=2&&text.substr(text.size()-2)=="·")||
        (text.size()>=3&&text.substr(text.size()-3)=="・"));
}
bool resolve_policy(const NativeNameCulture* native_culture,bool marked,NamePolicy& policy) {
    const auto culture=native_culture?
        std::string_view(native_culture->key.data(),static_cast<std::size_t>(native_culture->key.size)):
        std::string_view();
    bool configured=culture_name_policy(culture,policy);
    if(marked&&!configured) policy.separator=NameSeparator::none;
    if(native_name_policy_lookup&&!culture.empty()) {
        const auto key="EU4_UNICODE_NAME_"+std::string(culture);
        if(const auto value=native_name_policy_lookup(key.c_str())) {
            std::size_t size=0;
            while(size<64&&value[size]) ++size;
            if(size<64&&parse_name_policy({value,size},policy)) configured=true;
        }
    }
    if(marked) policy.order=NameOrder::surname_first;
    return marked||configured;
}
}
bool parse_name_policy(std::string_view value,NamePolicy& policy) noexcept {
    auto parsed=policy;
    const auto order=word(value),separator=word(value);
    if(order=="surname_first") parsed.order=NameOrder::surname_first;
    else if(order=="given_first") parsed.order=NameOrder::given_first;
    else return false;
    if(separator=="auto") parsed.separator=NameSeparator::automatic;
    else if(separator=="none") parsed.separator=NameSeparator::none;
    else if(separator=="space") parsed.separator=NameSeparator::space;
    else if(separator=="middle_dot") parsed.separator=NameSeparator::middle_dot;
    else return false;
    if(!word(value).empty()) return false;
    policy=parsed;return true;
}
std::string format_person_name(std::string_view given,std::string_view family,NamePolicy policy) {
    const auto first=policy.order==NameOrder::surname_first?family:given;
    const auto last=policy.order==NameOrder::surname_first?given:family;
    std::string result(first);
    if(!first.empty()&&!last.empty()&&!ends_separator(first)&&!starts_separator(last)) {
        auto separator=policy.separator;
        if(separator==NameSeparator::automatic)
            separator=compact_initial(first)&&compact_initial(last)?NameSeparator::none:NameSeparator::space;
        if(separator==NameSeparator::space) result.push_back(' ');
        else if(separator==NameSeparator::middle_dot) result.append("·");
    }
    result.append(last);return result;
}
bool surname_first(std::string_view suffix,std::size_t& surname_offset) {
    if(suffix.size()>=3&&suffix.substr(0,3)==" \xc2\xbf") {
        surname_offset=3;return true;
    }
    if(suffix.size()>=2&&suffix[0]==' '&&static_cast<unsigned char>(suffix[1])==0xbf) {
        surname_offset=2;return true;
    }
    return false;
}
}

extern "C" void* append_person_name(eu4unicode::EngineString* given,
    const eu4unicode::EngineString* suffix,const eu4unicode::NativeNamePerson* person) noexcept {
    using namespace eu4unicode;
    const std::string_view family(suffix->data(),static_cast<std::size_t>(suffix->size));
    try {
        std::size_t start=0;
        const bool marked=surname_first(family,start);
        // Only the native split-name suffix has this leading space. Complete
        // names and unrelated strings never enter the formatting policy.
        if(family.empty()||family.front()!=' ')
            return native_name_append(given,family.data(),family.size());
        NamePolicy policy;
        if(!resolve_policy(person?person->culture:nullptr,marked,policy))
            return native_name_append(given,family.data(),family.size());
        if(!marked) start=1;
        // Preserve source components before assigning through the engine owner.
        const auto joined=format_person_name({given->data(),static_cast<std::size_t>(given->size)},
            family.substr(start),policy);
        return native_name_assign(given,joined.data(),joined.size());
    } catch(...) {
        if(native_name_log) native_name_log("Personal name formatting failed.");
        return native_name_append(given,family.data(),family.size());
    }
}

extern "C" void* append_generated_name(eu4unicode::EngineString* given,const char* family_data,
    std::uint64_t family_size,const eu4unicode::NativeNameCulture* culture) noexcept {
    using namespace eu4unicode;
    const std::string_view family(family_data,static_cast<std::size_t>(family_size));
    try {
        std::size_t start=0;
        if(family.substr(0,2)=="\xc2\xbf") start=2;
        else if(!family.empty()&&static_cast<unsigned char>(family.front())==0xbf) start=1;
        NamePolicy policy;
        if(family.empty()||!resolve_policy(culture,start!=0,policy))
            return native_name_append(given,family.data(),family.size());
        auto first=std::string_view(given->data(),static_cast<std::size_t>(given->size));
        // The generator appends one space after each given-name token before
        // selecting a separate surname. Remove only that final engine space.
        if(!first.empty()&&first.back()==' ') first.remove_suffix(1);
        const auto joined=format_person_name(first,family.substr(start),policy);
        return native_name_assign(given,joined.data(),joined.size());
    } catch(...) {
        if(native_name_log) native_name_log("Generated name formatting failed.");
        return native_name_append(given,family.data(),family.size());
    }
}
