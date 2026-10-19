#pragma once
#include "engine_string.hpp"
#include <cstddef>
#include <cstdint>

namespace eu4unicode {
// Verified EU4 1.37.5 Windows vector and callback ABI. The parser borrows the
// buffer and never retains or releases its storage.
struct NativeYamlBytes {const char* begin;const char* end;const char* capacity;};
static_assert(sizeof(NativeYamlBytes)==24&&offsetof(NativeYamlBytes,end)==8);
using NativeYamlLanguage=void(*)(const EngineString*,int,void*);
using NativeYamlKey=void(*)(const EngineString*,const EngineString*,int,int,void*);
using NativeYamlParse=void(*)(void*,const NativeYamlBytes*,const char*,NativeYamlLanguage,NativeYamlKey,void*);
extern NativeYamlParse original_yaml_parse;
void parse_utf8_yaml(void* file,const NativeYamlBytes* input,const char* name,
                     NativeYamlLanguage language,NativeYamlKey key,void* context);
std::uint64_t native_yaml_signatures() noexcept;
}
