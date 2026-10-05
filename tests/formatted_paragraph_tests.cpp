#include "formatted_paragraph.hpp"
#include "unicode_services.hpp"
#include "formatted_text.hpp"
#include "shaped_paragraph.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>

void check(bool value,const char* message) {
    if(!value) { std::cerr<<"FAIL: "<<message<<'\n';std::exit(1); }
}
namespace {
using namespace eu4unicode;
auto glyphs(const ShapedParagraph& paragraph) {
    std::map<std::tuple<std::string,std::uint16_t,int,int,int>,int> result;
    for(const auto& run:paragraph.runs()) for(std::size_t index=0;index<run.glyphs.size();++index)
        ++result[{run.font_family,run.glyphs[index],static_cast<int>(std::round(run.advances[index]*1000)),
            static_cast<int>(std::round(run.offsets[index].advance*1000)),
            static_cast<int>(std::round(run.offsets[index].ascender*1000))}];
    return result;
}
}
int main() {
    using namespace eu4unicode;
    const std::string raw=u8"§Yس§Rلا§!م§! £adm£ 123";
    auto content=std::make_shared<ParagraphText>(raw,true,[](std::string_view name) {
        check(name=="adm","icon resolver receives the native symbol name");return 18.f;
    });
    check(content->source()==raw&&content->visible()==u8"سلام \ufffc 123","formatting metadata leaves one logical paragraph");
    check(content->colors().size()==3&&content->colors()[1]=="Y"&&content->colors()[2]=="YR",
        "nested colors preserve the native push and pop contract");
    check(content->icons().size()==1&&content->icons()[0].command==u8"£adm£"&&content->icons()[0].advance==18,
        "native icon commands and advances are retained");
    check(content->source_byte(0)==0&&content->source_byte(content->visible().size())==raw.size(),
        "source byte mapping includes leading and trailing format commands");
    check(paragraph_color_transition("YR","YG")==u8"§!§G"&&paragraph_color_transition("YG","")==u8"§!§!",
        "visual color changes restore native nesting without leaking state");
    ParagraphText marks(u8"a§Y\u0301b");
    check(marks.styles().size()==2&&marks.styles()[0].style==0&&marks.styles()[0].text_length==3&&
        marks.styles()[1].text_start==3&&marks.colors()[marks.styles()[1].style]=="Y",
        "a color command cannot split a combining grapheme");
    ParagraphText unknown(u8"§Yس§Qل§!ام",true,{},[](unsigned char code){return code=='Y'||code=='!';});
    check(unknown.colors().size()==2&&unknown.styles().back().style==0,
        "unknown colors are ignored before processing native resets");
    ParagraphText reset(u8"س§!لام");
    check(reset.colors().size()==2&&reset.colors()[1]=="!","an unmatched reset selects the font's default color");
    const std::string literal=u8"§Yالعربية£adm£";
    ParagraphText plain(literal,false);
    check(plain.visible()==literal&&plain.icons().empty()&&plain.colors().size()==1,
        "the native formatted flag controls interpretation of commands");
    const std::string compiled=std::string(1,char(0xa7))+"Y"+u8"العربية"+char(0xa7)+"!";
    ParagraphText native(compiled);
    check(native.visible()==u8"العربية"&&needs_native_paragraph_shaping(compiled),
        "single-byte compiled color delimiters do not invalidate UTF-8 visible text");
    check(!needs_native_paragraph_shaping(std::string(u8"العربية")+char(0xff)),
        "invalid visible bytes are not accepted as native formatting");
    for(const auto& pair:std::vector<std::pair<std::string,std::string>>{
        {u8"سلام",u8"س§Yلا§!م"},{u8"हिन्दी",u8"हि§Yन्दी§!"},{u8"a\u0323\u0301b",u8"a§Y\u0323\u0301b§!"}}) {
        ShapedParagraph reference(pair.first,24,400,true);
        auto formatted=std::make_shared<ParagraphText>(pair.second);
        ShapedParagraph styled(pair.second,24,400,true,{},formatted);
        check(glyphs(reference)==glyphs(styled),"drawing colors preserve contextual glyph selection and offsets");
        check(std::abs(reference.metrics().width-styled.metrics().width)<.01f,
            "drawing colors do not change paragraph advances");
        for(const auto byte:grapheme_boundaries(pair.first)) {
            const auto source=formatted->source_byte(byte);
            check(formatted->visible_byte(source)==byte,"formatted caret source mapping is reversible at grapheme boundaries");
            check(std::abs(reference.caret(byte).x-styled.caret(source).x)<.01f,
                "color commands preserve physical caret positions");
        }
        check(!styled.selection(0,pair.second.size()).empty(),"formatted selection maps back to visible text geometry");
        const FormattedText boundaries(pair.second);
        for(float x=0;x<420;x+=7) {
            const auto hit=styled.hit_test(x,10);
            check(std::binary_search(boundaries.prefixes().begin(),boundaries.prefixes().end(),hit.byte_offset),
                "styled hit positions map to complete source graphemes and commands");
        }
        for(const auto& tile:rasterize_paragraph(styled,100,100))
            check(tile.style<formatted->colors().size(),"shaped raster tiles retain their native drawing style");
    }
    ShapedParagraph styled(raw,24,95,true,{},content);
    const std::string flagged=u8"§Yالعربية @FRA§! / हिन्दी @D01 £adm£";
    auto flag_content=std::make_shared<ParagraphText>(flagged,true,
        [](std::string_view){return 18.f;},ParagraphText::ColorLookup{},[](std::string_view tag) {
            check(tag=="FRA"||tag=="D01","flag resolver receives the three-byte country tag");return 22.f;
        });
    ShapedParagraph flag_layout(flagged,24,100,true,{},flag_content);
    check(flag_layout.objects().size()==3&&flag_layout.metrics().lines>1,
        "country flags, native icons and contextual scripts share bidi layout and wrapping");
    check(flag_content->icons()[0].command=="@FRA"&&flag_content->icons()[1].command=="@D01",
        "native country commands survive shaping without replacing the flag renderer");
    check(styled.text()==raw&&styled.objects().size()==1&&styled.metrics().lines>1,
        "formatted paragraphs wrap with native icon objects and preserve the source");
    const auto& object=styled.objects().front();
    check(object.width==18&&object.x>=-.01f&&object.x+object.width<=95.01f,
        "RTL inline objects occupy the measured line box");
    check(object.style<content->colors().size(),"inline object drawing retains its color effect");
    TextLayoutOptions effect_options{};effect_options.styles.push_back({0,2,1});
    TextLayout effect_layout(u8"سلام",24,400,100,L"Segoe UI",{},effect_options);
    for(bool file:{false,true}) {
        bool rejected=false;
        try {
            if(file) effect_layout.render_png("unused-formatted-paragraph.png");
            else effect_layout.rasterize();
        } catch(const std::logic_error&) { rejected=true; }
        check(rejected,"standalone renderers cannot reinterpret native metadata as Direct2D brushes");
    }
    const std::string hard=u8"§Yالعربية\r\n£adm£ हिन्दी§!\n";
    auto hard_content=std::make_shared<ParagraphText>(hard,true,[](std::string_view){return 18.f;});
    ShapedParagraph hard_lines(hard,24,500,true,{},hard_content);
    check(hard_lines.lines().size()==3&&hard_lines.lines().front().newline_length==2&&
        hard_lines.lines().back().text_start+hard_lines.lines().back().text_length==hard.size(),
        "styled hard breaks and trailing commands preserve complete source ranges");
    for(const auto sample:{u8"العربية @FRA",u8"العربية £adm£",u8"العربية\t",u8"العربية £broken"}) {
        bool rejected=false;
        try { ParagraphText bad(sample); } catch(const std::exception&) { rejected=true; }
        check(rejected,"unavailable icon metrics and unsupported commands retain the existing native path");
    }
    std::cout<<"Formatted paragraph colors, contextual glyphs, native icons, source mappings and wrapping checks passed.\n";
}
