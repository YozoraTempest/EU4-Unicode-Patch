#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

void check(bool value,const char* message) {
    if(!value) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
int main() {
    using namespace eu4unicode;
    const std::string combining=u8"Ae\u0301中𠀀🇨🇳👩‍👩‍👧‍👦Z";
    const auto positions=grapheme_boundaries(combining);
    check(positions.size()==8,"combining accents, supplementary Han, flags and ZWJ families are complete clusters");
    for(std::size_t i=1;i<positions.size();++i) {
        check(previous_grapheme(combining,positions[i])==positions[i-1],"backspace boundary");
        check(next_grapheme(combining,positions[i-1])==positions[i],"forward deletion boundary");
        check(valid_utf8(std::string_view(combining).substr(positions[i-1],positions[i]-positions[i-1])),"UTF-8 byte boundary");
    }
    check(previous_grapheme(combining,0)==0&&next_grapheme(combining,combining.size())==combining.size(),"text edges");
    auto edited=combining;
    while(!edited.empty()) edited.erase(previous_grapheme(edited,edited.size()));
    check(edited.empty(),"delete a mixed string by complete graphemes");
    const auto lines=line_boundaries(u8"中文，测试。下一句");
    check(std::find(lines.begin(),lines.end(),6)==lines.end(),"no break before Chinese comma");
    check(std::find(lines.begin(),lines.end(),15)==lines.end(),"no break before Chinese full stop");
    check(search_key(u8"École STRAẞE Ａ")==search_key(u8"e\u0301cole strasse A"),"canonical, full case and width matching");
    check(search_key(u8"𠀀中文")==u8"𠀀中文","search keeps supplementary Han");
    check(grapheme_boundaries("")==std::vector<std::size_t>{0},"empty input");
    bool rejected=false;
    try { grapheme_boundaries("\xed\xa0\x80"); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"boundary services reject invalid UTF-8");
    std::cout<<"ICU grapheme, line-break and Unicode search checks passed.\n";
}
