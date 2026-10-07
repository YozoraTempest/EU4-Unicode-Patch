#pragma once
#include "engine_string.hpp"
#include <string>
#include <string_view>

namespace eu4unicode {
// The legacy surname marker is meaningful only in the engine's name suffix.
bool surname_first(std::string_view suffix,std::size_t& surname_offset);
using NameAppend=void*(*)(EngineString*,const char*,std::uint64_t);
using NameAssign=void*(*)(EngineString*,const char*,std::uint64_t);
extern NameAppend native_name_append;
extern NameAssign native_name_assign;
}
extern "C" void* append_person_name(eu4unicode::EngineString* given,
    const eu4unicode::EngineString* suffix);
