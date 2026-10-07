#pragma once
#include "engine_string.hpp"
#include <string>
#include <string_view>

namespace eu4unicode {
enum class NameOrder { given_first, surname_first };
enum class NameSeparator { automatic, none, space, middle_dot };
struct NamePolicy {
    NameOrder order=NameOrder::given_first;
    NameSeparator separator=NameSeparator::space;
};
bool culture_name_policy(std::string_view culture,NamePolicy& policy) noexcept;
bool parse_name_policy(std::string_view value,NamePolicy& policy) noexcept;
std::string format_person_name(std::string_view given,std::string_view family,NamePolicy policy);

// Verified character/culture fields in EU4 1.37.5. Neither string is owned here.
struct NativeNameCulture {
    std::byte padding[0x48];
    EngineString key;
};
struct NativeNamePerson {
    std::byte padding[0x60];
    const NativeNameCulture* culture;
};
static_assert(offsetof(NativeNameCulture,key)==0x48);
static_assert(offsetof(NativeNamePerson,culture)==0x60);
// The legacy surname marker is meaningful only in the engine's name suffix.
bool surname_first(std::string_view suffix,std::size_t& surname_offset);
using NameAppend=void*(*)(EngineString*,const char*,std::uint64_t);
using NameAssign=void*(*)(EngineString*,const char*,std::uint64_t);
using NamePolicyLookup=const char*(*)(const char*);
extern NameAppend native_name_append;
extern NameAssign native_name_assign;
extern NamePolicyLookup native_name_policy_lookup;
extern void(*native_name_log)(const char*);
}
extern "C" void* append_person_name(eu4unicode::EngineString* given,
    const eu4unicode::EngineString* suffix,const eu4unicode::NativeNamePerson* person) noexcept;
extern "C" void* append_generated_name(eu4unicode::EngineString* given,const char* family,
    std::uint64_t family_size,const eu4unicode::NativeNameCulture* culture) noexcept;
