#pragma once
#include "engine_string.hpp"
#include <cstddef>
#include <cstdint>

namespace eu4unicode {
struct NativeScriptLexer {
    void** vtable;
    void* input;
    std::uint32_t previous_token;
    std::uint32_t token_type;
    char text[512];
    unsigned char flags[8];
};
static_assert(offsetof(NativeScriptLexer,input)==8);
static_assert(offsetof(NativeScriptLexer,token_type)==0x14);
static_assert(offsetof(NativeScriptLexer,text)==0x18);
static_assert(offsetof(NativeScriptLexer,flags)==0x218);

using NativeFileLexerConstruction=NativeScriptLexer*(*)(NativeScriptLexer*,EngineString*);
using NativeFileLexerModeConstruction=NativeScriptLexer*(*)(NativeScriptLexer*,EngineString*,int);
using NativeStreamLexerConstruction=NativeScriptLexer*(*)(NativeScriptLexer*,void*,bool);
extern NativeFileLexerConstruction original_script_file;
extern NativeFileLexerModeConstruction original_script_file_mode;
extern NativeStreamLexerConstruction original_script_stream;

// Consume one UTF-8 signature at the beginning of a script input. The native
// input owns its bytes; raw reads and absolute seeking do not change line state.
bool consume_script_bom(void* input);
NativeScriptLexer* construct_script_file(NativeScriptLexer* lexer,EngineString* path);
NativeScriptLexer* construct_script_file_mode(NativeScriptLexer* lexer,EngineString* path,int mode);
NativeScriptLexer* construct_script_stream(NativeScriptLexer* lexer,void* input,bool own_input);
}
