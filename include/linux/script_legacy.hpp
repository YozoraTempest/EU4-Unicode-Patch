#pragma once
#include "linux/native.hpp"
#include <cstddef>
namespace eu4unicode::linux_native {
// Verified CTextLexer and CBinLexer layout; CFile and CString ownership stays in the engine.
struct ScriptLexer {
    void** vtable;
    void* input;
    std::uint32_t previous_token,token_type;
    char text[512];
    unsigned char flags[8];
};
static_assert(offsetof(ScriptLexer,text)==0x18);
static_assert(offsetof(ScriptLexer,flags)==0x218);
bool import_script_token(ScriptLexer& lexer) noexcept;
bool import_binary_token(ScriptLexer& lexer) noexcept;
void prepare_script_legacy(Hooks& hooks);
void report_script_legacy_counts() noexcept;
}
