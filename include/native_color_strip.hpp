#pragma once
#include "engine_string.hpp"

namespace eu4unicode {
using ColorStringAssign=EngineString*(*)(EngineString*,const char*,std::uint64_t);
extern ColorStringAssign native_color_assign;
extern void(*native_color_log)(const char*);

// The native API constructs a fresh output; its source is a distinct string.
// Both allocation and destruction stay with the engine's string implementation.
EngineString* construct_colorless_text(const EngineString* source,EngineString* target) noexcept;
}
