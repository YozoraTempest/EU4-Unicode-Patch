// Run native ABI checks inside the actual EU4 ELF, before its game main.
// This helper is built for validation only and is never packaged for players.
#include "linux/native.hpp"
#include "linux/script_legacy.hpp"
#include <dlfcn.h>
#include <safetyhook.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
using eu4unicode::linux_native::String;
using eu4unicode::linux_native::ScriptLexer;
void check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
void construct(String* value,std::string_view text) {
    reinterpret_cast<void(*)(String*,const char*,int)>(0x254b1ea)(value,text.data(),static_cast<int>(text.size()));
}
void destroy(String* value) {reinterpret_cast<void(*)(String*)>(0xd95742)(value);}
String* month(String* result,const void*,int) {construct(result,"十一月");return result;}
String province_name{};
const String* province(const void*) {return &province_name;}
int fort(const void*) {return 1;}
int kind=0;
int combat_kind(const void*) {return kind;}
String* localize(String* result,const char* key,const void*,int) {
    const std::string_view name(key);
    std::printf("Native localization fixture key=%s\n",key);
    construct(result,name=="LONG_EU3_DATE_STRING"?"11 十一月 1444":kind==3?"之围":"之战");return result;
}
String* wrapped(String* result,const void*,const String* value) {construct(result,value->view());return result;}
String* full_wrapped(String* result,const void* buffer) {
    construct(result,reinterpret_cast<const String*>(static_cast<const char*>(buffer)+0x30)->view());return result;
}
struct Row {String text;unsigned char synthetic;unsigned char padding[7];};
static_assert(sizeof(Row)==40);
struct Rows {const Row* begin;const Row* end;const Row* capacity;};
Rows rows{};int redraws=0;
const Rows* get_rows(const void*) {return &rows;}
void redraw(void*) {++redraws;}
void yaml_tests() {
    reinterpret_cast<void(*)(bool)>(0x2365c2e)(false);
    int index=0;
    for(const auto bom:{std::string("\xef\xbb\xbf"),std::string()}) {
        for(const auto legacy:{false,true}) {
            const auto key="EU4_PARITY_"+std::to_string(index++);
            const auto value=legacy?std::string("\x10-N\x11\x0eN\x12\xc3\xb9R\x13it"):std::string("中一对絛𠀀");
            const auto source=bom+"l_english:\r\n "+key+":0 \""+value+"\"\r\n";
            reinterpret_cast<void(*)(const char*,int,const char*,bool,bool,bool)>(0x236785b)(
                source.data(),static_cast<int>(source.size()),"native-contract.yml",false,false,false);
            reinterpret_cast<void(*)(const char*)>(0x2365cad)("l_english");
            reinterpret_cast<void(*)()>(0x23679c3)();
            const auto* text=reinterpret_cast<const char*(*)(const char*)>(0x23677f0)(key.c_str());
            std::printf("YAML bom=%zu legacy=%d key=%s value=%s\n",bom.size(),legacy,key.c_str(),text?text:"(null)");
            check(text&&std::string_view(text)==(legacy?"中一对絛":"中一对絛𠀀"),"native YAML UTF-8/BOM and legacy import");
        }
    }
    std::puts("PASS native YAML: UTF-8/BOM, supplementary characters and old CP1252-wrapped escapes");
}
void lexer_tests() {
    for(const auto bom:{std::string(),std::string("\xef\xbb\xbf")}) {
        for(const auto quoted:{false,true}) {
            for(const auto legacy:{false,true}) {
                const auto value=legacy?std::string("\x10-N\x11\x0eN"):std::string("中一");
                auto source=bom+"name = "+(quoted?"\"":"")+value+(quoted?"\"":"")+"\n";
                alignas(16) std::array<unsigned char,512> file{};
                ScriptLexer lexer{};
                reinterpret_cast<void(*)(void*,unsigned char*,int,int,int,bool)>(0x2563d46)(
                    file.data(),reinterpret_cast<unsigned char*>(source.data()),static_cast<int>(source.size()),0,0,false);
                reinterpret_cast<void(*)(ScriptLexer*,void*,bool)>(0x255e3c8)(&lexer,file.data(),false);
                const auto next=reinterpret_cast<int(*)(ScriptLexer*)>(0x255e44e);
                check(next(&lexer)&&std::string(lexer.text)=="name","native lexer key/BOM");
                check(next(&lexer),"native lexer assignment");
                check(next(&lexer)&&std::string(lexer.text)=="中一","native quoted/unquoted UTF-8 and legacy payload");
                check(!next(&lexer),"native lexer EOF");
                reinterpret_cast<void(*)(void*)>(0x2563e4c)(file.data());
            }
        }
    }
    std::puts("PASS native CTextLexer: UTF-8/BOM, quoted/unquoted legacy tokens");
}
void date_tests() {
    auto month_hook=SafetyHookInline::create(reinterpret_cast<void*>(0x1fe8c9c),reinterpret_cast<void*>(month));
    auto localize_hook=SafetyHookInline::create(reinterpret_cast<void*>(0x1feeaaf),reinterpret_cast<void*>(localize));
    auto province_hook=SafetyHookInline::create(reinterpret_cast<void*>(0x1612348),reinterpret_cast<void*>(province));
    auto fort_hook=SafetyHookInline::create(reinterpret_cast<void*>(0x165ac5c),reinterpret_cast<void*>(fort));
    check(month_hook&&localize_hook&&province_hook&&fort_hook,"native localization fixture hooks");
    String input{},output{};
    construct(&input,"1444.11.11");int date=0;
    reinterpret_cast<void(*)(void*,const String*)>(0x1378da0)(&date,&input);
    destroy(&input);
    reinterpret_cast<String*(*)(String*,const void*)>(0x1379fd8)(&output,&date);
    check(output.view()=="1444年十一月","native month/year ordering");destroy(&output);
    reinterpret_cast<String*(*)(String*,const void*,bool)>(0x137952c)(&output,&date,false);
    std::printf("Native long date=%.*s\n",static_cast<int>(output.size),output.bytes);
    check(output.view()=="1444年十一月11日","native date ordering");destroy(&output);
    construct(&province_name,"北京");
    alignas(16) std::array<unsigned char,128> combat{};
    std::array<void*,32> methods{};methods[0x68/8]=reinterpret_cast<void*>(combat_kind);
    auto* table=methods.data();std::memcpy(combat.data(),&table,sizeof(table));
    int fake_province=0;auto* location=&fake_province;std::memcpy(combat.data()+0x28,&location,sizeof(location));
    for(const auto type:{0,3}) {
        kind=type;
        reinterpret_cast<String*(*)(String*,const void*)>(0xf05b32)(&output,combat.data());
        check(output.view()==(type==3?"北京之围":"北京之战"),"native battle/siege ordering");destroy(&output);
    }
    destroy(&province_name);
    std::puts("PASS native date/month-year and combat/siege title builders (controlled localization)");
}
void select_tests() {
    auto wrap_hook=SafetyHookInline::create(reinterpret_cast<void*>(0x20d69ac),reinterpret_cast<void*>(wrapped));
    check(static_cast<bool>(wrap_hook),"native wrap fixture hook");
    for(const auto text:{std::string(),std::string("A中𠀀é"),std::string("中文\r\nسلام\n")}) {
        alignas(16) std::array<unsigned char,0x180> buffer{};
        std::array<void*,80> methods{};
        methods[0xc0/8]=reinterpret_cast<void*>(full_wrapped);
        methods[0x1a8/8]=reinterpret_cast<void*>(get_rows);
        methods[0x208/8]=reinterpret_cast<void*>(redraw);
        auto* table=methods.data();std::memcpy(buffer.data(),&table,sizeof(table));
        auto* document=reinterpret_cast<String*>(buffer.data()+0x30);
        auto* selected=reinterpret_cast<String*>(buffer.data()+0x70);
        construct(document,text);construct(selected,"");
        std::size_t count=0;
        for(std::size_t position=0;position<text.size();++count) {
            const auto nl=text.find('\n',position);position=nl==text.npos?text.size():nl+1;
        }
        // Native CString inline pointers require stable row addresses.
        std::vector<Row> cache(count);
        std::size_t begin=0;
        for(auto& row:cache) {const auto nl=text.find('\n',begin);const auto end=nl==text.npos?text.size():nl+1;
            construct(&row.text,std::string_view(text).substr(begin,end-begin));begin=end;}
        rows={cache.data(),cache.data()+cache.size(),cache.data()+cache.size()};
        std::array<int,4> event{'a',0,1,0};
        const auto handled=reinterpret_cast<bool(*)(void*,const void*)>(0x20d568c)(buffer.data(),event.data());
        check(handled&&selected->view()==text&&document->view()==text,"native Ctrl+A exact document selection");
        check(static_cast<bool>(buffer[0x90])==!text.empty(),"native empty selection state");
        destroy(document);destroy(selected);for(auto& row:cache) destroy(&row.text);
    }
    check(redraws==3,"native selection redraw count");
    std::puts("PASS native Ctrl+A and CTextBuffer::Select: empty, Unicode and multiline documents");
}
int contract_main(int,char**,char**) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    try {
        yaml_tests();lexer_tests();date_tests();select_tests();
        reinterpret_cast<void(*)()>(0x2365c77)();
        std::puts("PASS all native contract checks; game main was not entered");return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"Native contract failure: %s\n",error.what());return 1;}
}
}
extern "C" int __libc_start_main(int(*)(int,char**,char**),int argc,char** argv,
    void(*init)(),void(*fini)(),void(*rtld_fini)(),void* stack_end) {
    using Start=int(*)(int(*)(int,char**,char**),int,char**,void(*)(),void(*)(),void(*)(),void*);
    const auto next=reinterpret_cast<Start>(dlsym(RTLD_NEXT,"__libc_start_main"));
    if(!next||!std::getenv("EU4_UNICODE_NATIVE_CONTRACT")) std::abort();
    return next(contract_main,argc,argv,init,fini,rtld_fini,stack_end);
}
