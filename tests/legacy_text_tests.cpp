#include "legacy_text.hpp"
#include "native_legacy_import.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace eu4unicode;
using namespace std::string_view_literals;
namespace {
void check(bool value,const char* message) {
    if(!value) {std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
std::string bytes(std::initializer_list<unsigned> input) {
    std::string result;for(const auto byte:input) result.push_back(static_cast<char>(byte));return result;
}
void decoded(std::string_view text,LegacyPayload mode,std::string_view expected,
             std::size_t sequences,std::size_t relocated=0) {
    const auto result=decode_legacy_text(text,mode);
    check(result.error==LegacyError::none&&result.text==expected,"decoded text");
    check(result.sequences==sequences&&result.relocated==relocated,"conversion counts");
    check(valid_utf8(result.text),"converted text is UTF-8");
    const auto again=decode_legacy_text(result.text,mode);
    check(again.text==result.text&&again.sequences==0,"conversion is idempotent");
}
EngineString borrowed(const std::string& value) {
    EngineString result{};
    result.storage.pointer=const_cast<char*>(value.c_str());
    result.size=value.size();result.capacity=std::max<std::size_t>(16,value.size());return result;
}
int registrations=0,errors=0,received_line=0,received_version=0;
bool received_replace=false;
void* received_collection=nullptr;
std::string registered_key,registered_value,error_key;
void registration(void* collection,const char* key,const char* value,int line,int version,bool replace) {
    ++registrations;received_collection=collection;received_line=line;received_version=version;
    received_replace=replace;registered_key=key;registered_value=value;
}
void diagnostic(std::string_view,int line,std::string_view key,std::size_t,const char*) {
    ++errors;received_line=line;error_key=key;
}
int next_token(NativeScriptLexer* lexer) {
    lexer->token_type=2;
    std::strcpy(lexer->text,"\x10-N");
    return 1;
}
int no_token(NativeScriptLexer*) {return 0;}
}
int main() {
    const auto wrapped=LegacyPayload::cp1252_in_utf8,raw=LegacyPayload::raw_bytes;
    for(const auto& text:{std::string(),std::string(u8"中文𠀀 العربية हिन्दी é §Y彩色§! @FRA £adm£ $COUNTRY$ \\n"),
                         std::string(u8"\ufeff中文\ue101\ue9ff")}) {
        decoded(text,wrapped,text,0);decoded(text,raw,text,0);
    }
    decoded(u8"§Y\x10-N\x11\x0eN\x12ùR\x13it§! £adm£ @FRA $COUNTRY$ \\n",wrapped,
            u8"§Y中一对絛§! £adm£ @FRA $COUNTRY$ \\n",4);
    decoded(std::string("\x10-N\x11\x0eN\x12\xf9R\x13it"),raw,u8"中一对絛",4);
    decoded(u8"\x10\x00€"sv,wrapped,u8"耀",1);
    decoded(bytes({0x10,0x01,0xe1}),raw,u8"ā",1,1);
    decoded(bytes({0x10,0,0xe1}),raw,u8"\ue100",1);
    decoded(bytes({0x10,0,0xea}),raw,u8"\uea00",1);
    decoded(u8"\x10=Ø\x10\x00Þ"sv,wrapped,u8"😀",2);
    decoded(bytes({0x10,0x3d,0xd8,0x10,0,0xde}),raw,u8"😀",2);
    decoded(u8"正常中文 / \x10-N / 𠀀 / @ENG",wrapped,u8"正常中文 / 中 / 𠀀 / @ENG",1);
    decoded(std::string(u8"中文 ")+"Ariq-B\xf6kid \x10-N",raw,u8"中文 Ariq-Bökid 中",1);
    for(const auto& invalid:{std::string("\x10"),std::string("\x11" "a"),std::string(u8"\x10中N"),
                            std::string(u8"\x10=Ø"),std::string(u8"\x10\x01Þ"),std::string("\x10" "aa\xff")}) {
        const auto result=decode_legacy_text(invalid,wrapped);
        check(result.error!=LegacyError::none&&result.text.empty(),"malformed values are atomic failures");
    }
    const auto bad=decode_legacy_text(u8"中文\x10-N\x10",wrapped);
    check(bad.error==LegacyError::truncated_escape&&bad.error_offset==9,"errors use source byte offsets");

    register_imported_text=registration;legacy_import_diagnostic=diagnostic;
    const std::string key="PROV183",value=u8"§Y\x10-N§! @FRA";
    auto k=borrowed(key),v=borrowed(value);
    int collection=0;
    NativeLocalizationContext context{72,true,{},&collection};
    import_legacy_localization(&k,&v,4,0,&context);
    check(registrations==1&&registered_key==key&&registered_value==u8"§Y中§! @FRA","localization receives converted copy");
    check(received_line==72&&received_version==4&&received_replace&&received_collection==&collection,
          "localization registration metadata is preserved");
    check(value==u8"§Y\x10-N§! @FRA","localization input stays unchanged");
    const std::string broken="\x10";v=borrowed(broken);
    import_legacy_localization(&k,&v,4,0,&context);
    check(registrations==1&&errors==1&&received_line==72&&error_key==key,"bad entry is rejected with its key and line");
    const std::string normal=u8"中文𠀀 é";v=borrowed(normal);
    import_legacy_localization(&k,&v,5,0,&context);
    check(registrations==2&&registered_value==normal,"valid entry after an error is still imported");

    NativeScriptLexer lexer{};
    lexer.previous_token=123;lexer.flags[0]=7;lexer.token_type=2;
    std::strcpy(lexer.text,"\x10-N");
    check(convert_legacy_script_token(&lexer)&&std::string(lexer.text)==u8"中","quoted script conversion");
    check(lexer.previous_token==123&&lexer.token_type==2&&lexer.flags[0]==7,"lookahead and classification stay intact");
    lexer.token_type=15;std::strcpy(lexer.text,"\x10-N");
    check(convert_legacy_script_token(&lexer)&&std::string(lexer.text)==u8"中","unquoted script conversion");
    lexer.token_type=9;std::strcpy(lexer.text,"\x10-N");
    check(convert_legacy_script_token(&lexer)&&std::string(lexer.text)=="\x10-N","non-text tokens stay intact");
    lexer.token_type=2;std::strcpy(lexer.text,"\x10");
    check(!convert_legacy_script_token(&lexer)&&lexer.text[0]==0,"bad script token terminates the native parse");
    const auto large=std::string(508,'\xf6')+"\x10-N";
    std::memcpy(lexer.text,large.c_str(),512);
    check(!convert_legacy_script_token(&lexer)&&lexer.text[0]==0,"expanded script values cannot overflow the native buffer");
    original_script_token=next_token;
    check(read_legacy_script_token(&lexer)==1&&std::string(lexer.text)==u8"中","native reader wrapper");
    original_script_token=no_token;
    check(read_legacy_script_token(&lexer)==0,"native EOF contract");
    const auto counts=legacy_import_counts();
    check(counts.localization==1&&counts.script==3&&counts.rejected==3,"import summary counts");
    std::cout<<"PASS: legacy decoding and import boundaries\n";
}
