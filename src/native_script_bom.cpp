#include "native_script_bom.hpp"
#include <utf8.h>
#include <array>
#include <cstdint>

namespace eu4unicode {
NativeFileLexerConstruction original_script_file=nullptr;
NativeFileLexerModeConstruction original_script_file_mode=nullptr;
NativeStreamLexerConstruction original_script_stream=nullptr;

bool consume_script_bom(void* input) {
    if(!input) return false;
    const auto vtable=*static_cast<void***>(input);
    using Position=std::int64_t(*)(void*);
    using Read=bool(*)(void*,void*,int);
    using Seek=bool(*)(void*,int);
    const auto position=reinterpret_cast<Position>(vtable[0x80/8]);
    if(position(input)!=0) return false;
    const auto read=reinterpret_cast<Read>(vtable[0x20/8]);
    std::array<char,3> header{};
    // A failed native raw read leaves the position unchanged, including inputs
    // shorter than the signature. It does not consume a partial prefix.
    if(!read(input,header.data(),static_cast<int>(header.size()))) return false;
    if(utf8::starts_with_bom(header.begin(),header.end())) return true;
    const auto seek=reinterpret_cast<Seek>(vtable[0x60/8]);
    seek(input,0);
    return false;
}

NativeScriptLexer* construct_script_file(NativeScriptLexer* lexer,EngineString* path) {
    const auto result=original_script_file(lexer,path);
    consume_script_bom(result->input);
    return result;
}
NativeScriptLexer* construct_script_file_mode(NativeScriptLexer* lexer,EngineString* path,int mode) {
    const auto result=original_script_file_mode(lexer,path,mode);
    consume_script_bom(result->input);
    return result;
}
NativeScriptLexer* construct_script_stream(NativeScriptLexer* lexer,void* input,bool own_input) {
    const auto result=original_script_stream(lexer,input,own_input);
    consume_script_bom(result->input);
    return result;
}
}
