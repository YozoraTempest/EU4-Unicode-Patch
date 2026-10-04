#include "native_script_bom.hpp"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace {
void check(bool value,const char* message) {
    if(!value) { std::cerr<<"FAIL: "<<message<<'\n';std::exit(1); }
}
struct Input {
    void** vtable;
    std::string bytes;
    int cursor=0,line=1,reads=0,seeks=0;
    unsigned char last=0;
    bool pushback=false;
};
std::int64_t position(void* input) { return static_cast<Input*>(input)->cursor; }
bool read(void* input,void* destination,int size) {
    auto& stream=*static_cast<Input*>(input);
    ++stream.reads;
    if(static_cast<std::size_t>(stream.cursor)+size>stream.bytes.size()) return false;
    std::memcpy(destination,stream.bytes.data()+stream.cursor,static_cast<std::size_t>(size));
    stream.cursor+=size;
    return true;
}
bool seek(void* input,int offset) {
    auto& stream=*static_cast<Input*>(input);
    ++stream.seeks;
    if(offset<0||static_cast<std::size_t>(offset)>stream.bytes.size()) return false;
    stream.cursor=offset;
    return true;
}
std::array<void*,17> methods=[] {
    std::array<void*,17> result{};
    result[0x20/8]=reinterpret_cast<void*>(read);
    result[0x60/8]=reinterpret_cast<void*>(seek);
    result[0x80/8]=reinterpret_cast<void*>(position);
    return result;
}();
void check_input(std::string bytes,bool signature) {
    Input input{methods.data(),bytes};
    check(eu4unicode::consume_script_bom(&input)==signature,"signature detection");
    check(input.cursor==(signature?3:0),"only a leading signature advances the input");
    check(input.bytes==bytes,"source bytes remain intact");
    check(input.line==1&&input.last==0&&!input.pushback,"line and character state remain intact");
    const auto reads=input.reads;
    if(signature) {
        check(!eu4unicode::consume_script_bom(&input)&&input.reads==reads,
              "an already positioned input is not consumed twice");
    }
    std::array<char,3> next{};
    const auto remaining=bytes.size()-static_cast<std::size_t>(input.cursor);
    if(remaining>=next.size()) {
        check(read(&input,next.data(),3),"next read succeeds");
        check(std::string_view(next.data(),3)==std::string_view(bytes).substr(signature?3:0,3),
              "the parser receives the original first content bytes");
    }
}
Input* constructed_input=nullptr;
eu4unicode::EngineString* received_path=nullptr;
int received_mode=0;
bool received_ownership=false;
eu4unicode::NativeScriptLexer* file_constructor(eu4unicode::NativeScriptLexer* lexer,
                                               eu4unicode::EngineString* path) {
    received_path=path;
    lexer->input=constructed_input;
    return lexer;
}
eu4unicode::NativeScriptLexer* mode_constructor(eu4unicode::NativeScriptLexer* lexer,
                                               eu4unicode::EngineString* path,int mode) {
    received_mode=mode;
    return file_constructor(lexer,path);
}
eu4unicode::NativeScriptLexer* stream_constructor(eu4unicode::NativeScriptLexer* lexer,
                                                 void* input,bool ownership) {
    received_ownership=ownership;
    lexer->input=input;
    return lexer;
}
}
int main() {
    const std::string bom="\xef\xbb\xbf";
    for(const auto& body:{std::string("# comment\nowner = SWE\n"),std::string("owner = SWE\r\n"),
                         std::string(u8"name = \"中文𠀀\"\n"),std::string("guiTypes = {\n}"),
                         std::string("spriteTypes = {\n}"),std::string("country_event = {\n}"),
                         std::string(u8"name = \"a\ufeffb\"\n")}) {
        check_input(body,false);
        check_input(bom+body,true);
    }
    check_input("",false);
    check_input("\xef",false);
    check_input("\xef\xbb",false);
    check_input("\xef\xbbx",false);
    check_input("\xff\xfe",false);
    check_input(bom,true);
    check_input(bom+bom+"owner = SWE",true);
    check_input(" "+bom+"owner = SWE",false);
    check_input("x"+bom+"owner = SWE",false);
    check(!eu4unicode::consume_script_bom(nullptr),"failed file-open input remains absent");
    Input positioned{methods.data(),bom+"owner = SWE"};
    positioned.cursor=1;
    check(!eu4unicode::consume_script_bom(&positioned)&&positioned.cursor==1&&positioned.reads==0,
          "nonzero input positions retain their native contract");

    eu4unicode::original_script_file=file_constructor;
    eu4unicode::original_script_file_mode=mode_constructor;
    eu4unicode::original_script_stream=stream_constructor;
    eu4unicode::EngineString path{};
    eu4unicode::NativeScriptLexer lexer{};
    Input file{methods.data(),bom+"owner = SWE"};
    constructed_input=&file;
    check(eu4unicode::construct_script_file(&lexer,&path)==&lexer&&received_path==&path,
          "file constructor forwards path and return pointer");
    check(file.cursor==3,"file input starts after the signature");
    file.cursor=0;
    check(eu4unicode::construct_script_file_mode(&lexer,&path,2)==&lexer&&received_mode==2,
          "file mode is forwarded unchanged");
    check(file.cursor==3,"file mode input starts after the signature");
    file.cursor=0;
    check(eu4unicode::construct_script_stream(&lexer,&file,true)==&lexer&&received_ownership,
          "stream constructor forwards ownership and return pointer");
    check(file.cursor==3,"stream input starts after the signature");
    constructed_input=nullptr;
    check(eu4unicode::construct_script_file(&lexer,&path)==&lexer&&lexer.input==nullptr,
          "file-open failure retains the native lexer");
    std::cout<<"PASS: script UTF-8 signatures, source preservation and native lexer construction.\n";
}
