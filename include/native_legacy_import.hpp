#pragma once
#include "engine_string.hpp"
#include "native_script_bom.hpp"
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace eu4unicode {
struct NativeLocalizationContext {
    int line;
    bool replace;
    char padding[11];
    void* collection;
};
static_assert(offsetof(NativeLocalizationContext,collection)==16);
using NativeTextRegistration=void(*)(void*,const char*,const char*,int,int,bool);
using NativeScriptTokenRead=int(*)(NativeScriptLexer*);
using LegacyImportDiagnostic=void(*)(std::string_view source,int line,
                                    std::string_view key,std::size_t offset,const char* error);
extern NativeTextRegistration register_imported_text;
extern NativeScriptTokenRead original_script_token;
extern LegacyImportDiagnostic legacy_import_diagnostic;
struct LegacyImportCounts {
    std::uint64_t localization,script,sequences,relocated,rejected;
};
LegacyImportCounts legacy_import_counts() noexcept;
void import_legacy_localization(const EngineString* key,const EngineString* value,
                               int version,int unused,const NativeLocalizationContext* context) noexcept;
// Decode only completed text tokens. The native lexer retains ownership of
// its input, token classification, fixed buffer and lookahead state.
bool convert_legacy_script_token(NativeScriptLexer* lexer) noexcept;
int read_legacy_script_token(NativeScriptLexer* lexer);
}
