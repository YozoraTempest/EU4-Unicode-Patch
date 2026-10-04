#pragma once
#include "engine_string.hpp"
#include <cstdint>

namespace eu4unicode {
using NativeFindText=std::uint64_t(*)(const char*,std::uint64_t,std::uint64_t,const char*,std::uint64_t);
using NativeSearchDistance=std::int64_t(*)(const EngineString*,const EngineString*);
bool display_find_caller(std::uintptr_t caller) noexcept;
bool province_distance_caller(std::uintptr_t caller) noexcept;
std::uint64_t find_display_name(std::uintptr_t caller,const char* name,std::uint64_t length,
    std::uint64_t start,const char* query,std::uint64_t query_length,NativeFindText original);
std::int64_t province_search_distance(std::uintptr_t caller,const EngineString* name,
    const EngineString* query,NativeSearchDistance original);
}
