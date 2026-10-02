#include "unicode_search.hpp"
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
    bool rejected=false;
    try { country_search_contains("\xed\xa0\x80","x"); }
    catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"invalid UTF-8 rejected before matching");
    std::cout<<"Unicode country display-name search checks passed.\n";
}
