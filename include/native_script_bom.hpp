#pragma once
#include "engine_string.hpp"
#include <cstddef>

namespace eu4unicode {
struct NativeScriptLexer {
    void** vtable;
    void* input;
};
static_assert(offsetof(NativeScriptLexer,input)==8);

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
