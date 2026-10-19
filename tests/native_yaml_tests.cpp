#include "native_yaml.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace eu4unicode;
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
struct Context {int calls=0,languages=0,keys=0;};
Context expected_context;
int file_object;
const char name[]="localisation/test_l_english.yml";
std::string expected_bytes;
const NativeYamlBytes* expected_buffer=nullptr;
void language(const EngineString*,int version,void* context) {
    check(context==&expected_context&&version==7,"language callback context preserved");
    ++expected_context.languages;
}
void key(const EngineString*,const EngineString*,int version,int line,void* context) {
    check(context==&expected_context&&version==9&&line==3,"key callback context preserved");
    ++expected_context.keys;
}
void parser(void* file,const NativeYamlBytes* input,const char* filename,
            NativeYamlLanguage language_callback,NativeYamlKey key_callback,void* context) {
    check(file==&file_object&&filename==name&&context==&expected_context,"native parser arguments preserved");
    check(language_callback==language&&key_callback==key,"native callback identities preserved");
    check(std::string(input->begin,input->end)==expected_bytes,"native parser receives expected bytes");
    check(input->capacity>=input->end,"borrowed buffer has native vector bounds");
    if(expected_buffer) check(input==expected_buffer,"unchanged buffers retain native identity");
    else check(input->end-input->begin==input->capacity-input->begin,"normalized buffer bounds exact");
    ++expected_context.calls;
    language_callback(nullptr,7,context);
    key_callback(nullptr,nullptr,9,3,context);
}
}
int main() {
    original_yaml_parse=parser;
    for(const auto text:{std::string(u8"l_english:\n key:0 \"中文𠮷\"\n"),
                         std::string("l_english:\n key:0 \"ASCII\"\n")}) {
        for(const bool bom:{false,true}) {
            const auto source=(bom?std::string("\xef\xbb\xbf"):std::string())+text;
            const NativeYamlBytes input{source.data(),source.data()+source.size(),source.data()+source.size()};
            expected_bytes="\xef\xbb\xbf"+text;
            expected_buffer=bom?&input:nullptr;
            parse_utf8_yaml(&file_object,&input,name,language,key,&expected_context);
            check(source==(bom?std::string("\xef\xbb\xbf"):std::string())+text,"source storage is immutable");
        }
    }
    // Keep invalid buffers intact so rejection remains the native/import policy.
    for(const auto source:{std::string(),std::string("\xc0\xaf")}) {
        const NativeYamlBytes input{source.data(),source.data()+source.size(),source.data()+source.size()};
        expected_bytes=source;expected_buffer=&input;
        parse_utf8_yaml(&file_object,&input,name,language,key,&expected_context);
    }
    check(expected_context.calls==6&&expected_context.languages==6&&expected_context.keys==6,"callbacks forwarded once per parse");
    check(native_yaml_signatures()==2,"only missing valid signatures counted");
    std::cout<<"Windows YAML adapter buffer ownership and callback contract passed\n";
}
