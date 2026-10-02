#include "unicode_layout.hpp"
#include "unicode_services.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>

void check(bool value,const char* message) {
    if(!value) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
int main(int argc,char** argv) {
    using namespace eu4unicode;
    const std::string text=u8"Unicode: 中文 日本語 한국어 𠀀 😀\nLatin: e\u0301 É ß — Greek: Ελληνικά\nArabic: العربية — Hebrew: עברית\nDevanagari: हिन्दी — Thai: ภาษาไทย";
    TextLayout layout(text,28,980,800);
    const auto measured=layout.metrics();
    check(measured.width>0&&measured.height>0&&measured.lines>=4,"mixed text has measured lines");
    const auto runs=layout.glyph_runs();
    check(!runs.empty(),"layout produces glyph runs");
    bool rtl=false;
    std::set<std::string> fonts;
    std::size_t missing=0;
    for(const auto& run:runs) {
        fonts.insert(run.font_family);
        rtl=rtl||(run.bidi_level%2!=0);
        check(run.text_start+run.text_length<=text.size(),"glyph source offsets stay in UTF-8 text");
        missing+=std::count(run.glyphs.begin(),run.glyphs.end(),0);
    }
    check(rtl,"Arabic and Hebrew produce RTL runs");
    check(fonts.size()>1,"system font fallback selects multiple font families");
    const auto boundaries=grapheme_boundaries(text);
    for(float y=0;y<measured.height;y+=11) for(float x=-10;x<measured.width+10;x+=9) {
        const auto hit=layout.hit_test(x,y);
        check(std::binary_search(boundaries.begin(),boundaries.end(),hit.byte_offset),"hit testing selects a complete UTF-8 grapheme");
    }
    bool rejected=false;
    try { TextLayout bad("\xed\xa0\x80",20,100,100); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"renderer rejects malformed UTF-8");
    if(argc==2) layout.render_png(argv[1]);
    std::cout<<"DirectWrite layout, fallback, bidi and hit-test checks passed. Missing glyphs: "<<missing<<"\n";
    for(const auto& family:fonts) std::cout<<family<<'\n';
}
