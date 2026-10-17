#include "native_name_order.hpp"

namespace eu4unicode {
NameAppend native_name_append=nullptr;
NameAssign native_name_assign=nullptr;
NamePolicyLookup native_name_policy_lookup=nullptr;
void(*native_name_log)(const char*)=nullptr;
namespace {
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
