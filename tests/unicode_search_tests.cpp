#include "unicode_search.hpp"
#include "unicode_pinyin.hpp"
#include "unicode_services.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

void check(bool value,const char* message) {
    if(!value) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
int main() {
    using eu4unicode::country_search_contains;
    check(country_search_contains(u8"法兰西𠀀",u8"兰西𠀀"),"Chinese and supplementary substring");
    check(!country_search_contains(u8"法兰西𠀀",u8"𐀀"),"supplementary planes remain distinct");
    check(country_search_contains(u8"ÉCOLE",u8"e\u0301cole"),"canonical and case matching");
    check(country_search_contains(u8"Åland",u8"aland"),"existing accent-insensitive country search");
    check(country_search_contains(u8"Æsir",u8"asir"),"existing native AE-to-A equivalence");
    check(country_search_contains(u8"Straße",u8"strase"),"existing native sharp-s-to-S equivalence");
    check(country_search_contains(u8"Straße",u8"strasse"),"full Unicode casefold expansion");
    check(country_search_contains(u8"ＪＡＰＡＮ",u8"japan"),"compatibility width matching");
    check(country_search_contains(u8"ΕΛΛΑΔΑ",u8"ελλαδα"),"Greek case matching");
    check(country_search_contains(u8"МОСКВА",u8"москва"),"Cyrillic case matching");
    check(country_search_contains("","")&&!country_search_contains("","France"),"empty search semantics");
    for(const auto query:{"falanxi","flx","fl","falan","lanxi","fa lan xi","FA-LAN-XI","f\xc7\x8e lan xi"})
        check(country_search_contains(u8"法兰西",query),"full, initial, partial and normalized pinyin");
    check(country_search_contains(u8"法兰西",u8"法lanxi"),"mixed Chinese and pinyin");
    check(country_search_contains(u8"法兰西",u8"fa兰x"),"mixed literal, full syllable and initial");
    check(!country_search_contains(u8"法兰西","f"),"one letter does not expand to pinyin matches");
    check(!country_search_contains(u8"法兰西","alan"),"pinyin starts at syllable boundaries");
    check(!country_search_contains(u8"法兰西",u8"花兰西"),"Chinese literals do not match homophones");
    check(!country_search_contains(u8"法兰西","falanix"),"typo tolerance is not implicitly enabled");
    check(country_search_contains(u8"奥地利","aodili")&&country_search_contains(u8"奥地利","adl"),"Austrian phrase pronunciation");
    check(country_search_contains(u8"勃兰登堡","bldb"),"longer initial sequence");
    check(country_search_contains(u8"重庆","chongqing")&&country_search_contains(u8"重庆","cq"),"Chongqing phrase pronunciation");
    check(country_search_contains(u8"长安","changan")&&country_search_contains(u8"西藏","xizang"),"geographic polyphones");
    check(country_search_contains(u8"重慶","chongqing")&&country_search_contains(u8"法蘭西",u8"法兰西"),"traditional names and simplified queries");
    for(const auto query:{"lvsong","lusong",u8"lǚsòng"}) check(country_search_contains(u8"吕宋",query),"umlaut spelling variants");
    check(country_search_contains(u8"新法兰西","xflx")&&country_search_contains(u8"新法兰西","flx"),"compound and dynamic names");
    check(country_search_contains(u8"New 法兰西","newflx"),"mixed Latin display name");
    check(country_search_contains(u8"École 法兰西","ecoleflx"),"accent-insensitive mixed display name");
    check(country_search_contains(u8"§Y法兰西§!","flx"),"display formatting does not enter the index");
    check(country_search_contains(u8"𠮷野",u8"𠮷"),"unmapped Han remains literally searchable");
    check(country_search_contains(u8"旧法兰西","flx")&&!country_search_contains(u8"新奥地利","flx"),"renamed content uses current display text");
    check(eu4unicode::display_search_distance(u8"长安",u8"長安")==0&&
        eu4unicode::display_search_distance(u8"长安","ca")==1&&
        eu4unicode::display_search_distance(u8"长安","paris")==-1,"province ranking contract");
    std::string repeated;
    for(int i=0;i<120;++i) repeated+=u8"中";
    check(eu4unicode::transliterated_text(repeated,"Han-Latin").size()>repeated.size(),"transliterator expands its output buffer");
    eu4unicode::set_pinyin_dictionary(u8"\ufeff# mod names\r\n重庆: zhòng qìng\r\n重庆: chóng qìng\r\n𠮷野: ji ye\n");
    check(country_search_contains(u8"重庆","zhongqing")&&country_search_contains(u8"重庆","chongqing"),"optional BOM dictionary adds complete readings");
    check(country_search_contains(u8"𠮷野","jy"),"optional supplementary pronunciation");
    bool bad_dictionary=false;
    try { eu4unicode::set_pinyin_dictionary(u8"重庆: chong\n"); } catch(const std::invalid_argument&) { bad_dictionary=true; }
    check(bad_dictionary&&country_search_contains(u8"𠮷野","jy"),"invalid dictionary replacement is atomic");
    eu4unicode::set_pinyin_dictionary(u8"重庆: chong qing | zhong jing\n");
    check(country_search_contains(u8"重庆","chongqing")&&country_search_contains(u8"重庆","zhongjing")&&
        !country_search_contains(u8"重庆","chongjing"),"phrase alternatives keep their syllables together");
    eu4unicode::set_pinyin_dictionary("");
    check(!country_search_contains(u8"重庆","zhongqing")&&!country_search_contains(u8"𠮷野","jy"),"dictionary reload invalidates cached names");
    for(int i=0;i<1600;++i) check(country_search_contains(u8"法兰西"+std::to_string(i),"flx"),"large mod names exceed the previous cache count");
    check(country_search_contains(u8"法兰西0","flx"),"large lists remain searchable after repeated queries");
    bool rejected=false;
    try { country_search_contains("\xed\xa0\x80","x"); }
    catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"invalid UTF-8 rejected before matching");
    std::cout<<"Unicode country display-name search checks passed.\n";
}
