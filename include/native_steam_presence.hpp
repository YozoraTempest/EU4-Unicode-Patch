#pragma once
#include "engine_string.hpp"

namespace eu4unicode {
inline constexpr std::uintptr_t steam_presence_conversion_return=0xa90203;
using NativePresenceConversion=EngineString*(*)(EngineString*,const EngineString*);
using NativeStringAssignment=EngineString*(*)(EngineString*,const char*,std::uint64_t);

// The native conversion constructs a fresh destination. Only the Steam caller
// receives UTF-8 directly; the other observed caller keeps native conversion.
EngineString* construct_steam_presence(EngineString* target,const EngineString* source,
    std::uintptr_t caller_rva,NativePresenceConversion convert,NativeStringAssignment assign);
}
