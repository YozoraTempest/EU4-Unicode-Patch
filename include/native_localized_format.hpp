#pragma once
#include "engine_string.hpp"
#include <cstdint>

namespace eu4unicode {
using NativeDateFormat=EngineString*(*)(const void*,EngineString*,const EngineString*);
using NativeDateConcat=EngineString*(*)(EngineString*,const EngineString*,const EngineString*);
using NativeBattleTitle=EngineString*(*)(const void*,EngineString*);
using LocalizedAssign=void*(*)(EngineString*,const char*,std::uint64_t);
extern NativeDateFormat original_localized_date;
extern NativeDateConcat original_localized_date_concat;
extern NativeBattleTitle original_localized_battle_title;
extern LocalizedAssign localized_assign;
void configure_localized_format(const void* image) noexcept;
EngineString* format_localized_date(const void* date,EngineString* result,const EngineString* format);
EngineString* concat_localized_date(EngineString* result,const EngineString* month,const EngineString* year);
EngineString* format_localized_battle_title(const void* battle,EngineString* result);
}
