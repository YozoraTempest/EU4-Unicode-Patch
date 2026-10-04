#include "shaped_paragraph.hpp"
#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>

void check(bool value,const char* message) {
    if(!value) { std::cerr<<"FAIL: "<<message<<'\n';std::exit(1); }
}
int main() {
    using namespace eu4unicode;
    for(const auto text:{u8"中文𠮷",u8"Latin Ελληνικά русский",u8"한국어 日本語"})
        check(!needs_paragraph_shaping(text),"ordinary native metrics remain selected");
    for(const auto text:{u8"العربية",u8"עברית",u8"हिन्दी",u8"ภาษาไทย",u8"a\u0323\u0301",u8"👩‍💻",u8"A\u2067123\u2069"})
        check(needs_paragraph_shaping(text),"contextual, combining and bidi text selects paragraph shaping");
    check(!needs_paragraph_shaping(u8"العربية\xff"),"malformed suffix is not accepted as Unicode");
    check(plain_native_paragraph(u8"العربية\r\nLatin"),"explicit native line separators are accepted");
    for(const auto text:{u8"§Yالعربية§!",u8"£adm£ العربية",u8"@ABC العربية",u8"{12 العربية",u8"العربية\tA"})
        check(!plain_native_paragraph(text),"native formatting stays with its existing renderer");
    const std::string text=u8"العربية 123 English\r\nLatin a\u0323\u0301\nעברית 45\n";
    ShapedParagraph paragraph(text,24,420,true);
    check(paragraph.text()==text&&paragraph.metrics().lines==4,"original UTF-8 and trailing empty line are retained");
    const auto& lines=paragraph.lines();
    std::size_t offset=0;
    for(const auto& line:lines) {
        check(line.text_start==offset&&line.height==24,"line ranges cover source bytes on the native grid");
        check(valid_utf8(std::string_view(text).substr(line.text_start,line.text_length)),"line ranges contain complete UTF-8 scalars");
        offset+=line.text_length;
    }
    check(offset==text.size()&&lines.front().newline_length==2,"CRLF source positions are preserved");
    check(lines[1].newline_length==1&&lines.back().text_length==0,"single separators and final empty paragraph are preserved");
    ShapedParagraph separators(u8"العربية\u2029English\u0085עברית\u2028Latin",24,420,true);
    check(separators.lines().size()==4&&separators.lines()[0].newline_length==3&&
        separators.lines()[1].newline_length==2&&separators.lines()[2].newline_length==3,
        "Unicode hard separators preserve byte ranges and independent paragraph direction");
    bool rtl=false,joined=false;
    for(const auto& run:paragraph.runs()) {
        rtl=rtl||(run.bidi_level%2!=0);
        check(run.text_start+run.text_length<=text.size(),"run offsets refer to the original paragraph");
        for(const auto& cluster:run.clusters) {
            const auto source=std::string_view(text).substr(cluster.text_start,cluster.text_length);
            check(valid_utf8(source),"shaped clusters retain UTF-8 boundaries");
            joined=joined||decode(source).bytes<source.size();
        }
    }
    check(rtl&&joined,"RTL runs and combined clusters reach the paragraph bridge");
    const auto tiles=rasterize_paragraph(paragraph,17,11);
    check(!tiles.empty(),"shaped paragraphs produce atlas tiles");
    for(const auto& tile:tiles) {
        check(tile.bitmap.width<=17&&tile.bitmap.height<=11,"texture boundaries split ink without splitting source text");
        check(tile.line<lines.size(),"every atlas tile belongs to a measured line");
        const auto run=std::find_if(paragraph.runs().begin(),paragraph.runs().end(),[&](const GlyphRun& candidate) {
            return candidate.text_start==tile.text_start&&candidate.text_length==tile.text_length;
        });
        check(run!=paragraph.runs().end(),"tile retains its shaped run source range");
        const auto image=rasterize_glyph_run(*run);
        const auto x=static_cast<std::uint32_t>(tile.x-std::floor(run->baseline_x)-image.left);
        const auto y=static_cast<std::uint32_t>(tile.y-std::floor(run->baseline_y)-image.top);
        check(x+tile.bitmap.width<=image.width&&y+tile.bitmap.height<=image.height,"tile has an exact shaped raster position");
        for(std::uint32_t row=0;row<tile.bitmap.height;++row) for(std::uint32_t col=0;col<tile.bitmap.width;++col)
            check(tile.bitmap.alpha[static_cast<std::size_t>(row)*tile.bitmap.width+col]==
                image.alpha[static_cast<std::size_t>(y+row)*image.width+x+col],"tiling retains contextual glyph pixels");
    }
    const auto boundaries=grapheme_boundaries(text);
    for(float y=-4;y<paragraph.metrics().height+4;y+=9) for(float x=-4;x<430;x+=13) {
        const auto hit=paragraph.hit_test(x,y);
        check(std::binary_search(boundaries.begin(),boundaries.end(),hit.byte_offset),"paragraph hit tests return original UTF-8 grapheme boundaries");
    }
    ShapedParagraph arabic(u8"العربية",24,600,true);
    float right=0;
    for(const auto& tile:rasterize_paragraph(arabic,100,100)) right=(std::max)(right,tile.x+tile.bitmap.width);
    check(right<arabic.metrics().width+24,"RTL ink stays at the physical left origin instead of the far layout edge");
    ShapedParagraph narrow(u8"العربية العربية العربية",24,85,true);
    ShapedParagraph unwrapped(u8"العربية العربية العربية",24,85,false);
    check(narrow.metrics().lines>1&&unwrapped.metrics().lines==1,"wrapping is controlled by the paragraph layout");
    for(const auto& line:narrow.lines()) check(line.width<=110,"wrapped line widths follow the available UI box");
    bool invalid=false,budget=false,spacing=false;
    try { ShapedParagraph bad("\xff",20,100,true); } catch(const std::invalid_argument&) { invalid=true; }
    try { ShapedParagraph bad(std::string(501,'\n'),20,100,true); } catch(const std::length_error&) { budget=true; }
    try { TextLayout bad("A",20,100,100,L"Segoe UI",{},
        TextLayoutOptions{TextDirection::Automatic,true,std::numeric_limits<float>::quiet_NaN()}); }
    catch(const std::invalid_argument&) { spacing=true; }
    check(invalid&&budget&&spacing,"invalid UTF-8, excessive line counts and invalid spacing are rejected");
    std::cout<<"Paragraph direction, shaping, UTF-8 mapping, line metrics and atlas tiling checks passed.\n";
}
