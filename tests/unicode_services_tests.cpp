#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <icu.h>

void check(bool value,const char* message) {
    if(!value) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
std::vector<std::size_t> fresh_boundaries(std::string_view text,UBreakIteratorType type) {
    UErrorCode status=U_ZERO_ERROR;
    std::unique_ptr<UText,decltype(&utext_close)> source(utext_openUTF8(nullptr,text.data(),
        static_cast<int64_t>(text.size()),&status),utext_close);
    check(U_SUCCESS(status),"reference text construction");
    std::unique_ptr<UBreakIterator,decltype(&ubrk_close)> iterator(
        ubrk_open(type,"",nullptr,0,&status),ubrk_close);
    check(U_SUCCESS(status),"reference iterator construction");
    ubrk_setUText(iterator.get(),source.get(),&status);
    check(U_SUCCESS(status),"reference text binding");
    std::vector<std::size_t> result;
    for(auto offset=ubrk_first(iterator.get());offset!=UBRK_DONE;offset=ubrk_next(iterator.get()))
        result.push_back(static_cast<std::size_t>(offset));
    return result;
}
void check_iterator_reuse() {
    using namespace eu4unicode;
    const std::vector<std::string> texts{
        u8"中文，测试。下一句",u8"السلام عليكم 123",u8"हिन्दी नमस्ते दुनिया",
        u8"ภาษาไทยทดสอบ",u8"Ae\u0301𠀀🇨🇳👩‍👩‍👧‍👦", "x", "",
        std::string("A\0B",3),std::string(8192,'a')+u8"中文"};
    std::vector<std::vector<std::size_t>> characters,lines;
    for(const auto& text:texts) {
        characters.push_back(fresh_boundaries(text,UBRK_CHARACTER));
        lines.push_back(fresh_boundaries(text,UBRK_LINE));
    }
    const auto run=[&](std::size_t seed) {
        for(std::size_t repeat=0;repeat<100;++repeat) {
            const auto index=(repeat+seed)%texts.size();
            auto temporary=texts[index];
            check(grapheme_boundaries(temporary)==characters[index],"reused iterator changed grapheme boundaries");
            check(line_boundaries(temporary)==lines[index],"reused iterator changed line boundaries");
            temporary.assign(20000,'z');
            bool rejected=false;
            try { line_boundaries("\xed\xa0\x80"); } catch(const std::invalid_argument&) { rejected=true; }
            check(rejected,"reused iterator accepted invalid text");
            check(grapheme_boundaries("x")==std::vector<std::size_t>{0,1},"iterator retained previous caller text");
        }
    };
    run(0);
    std::vector<std::future<void>> threads;
    for(std::size_t seed=0;seed<8;++seed)
        threads.push_back(std::async(std::launch::async,run,seed));
    for(auto& thread:threads) thread.get();
}
int main() {
    using namespace eu4unicode;
    check_iterator_reuse();
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
