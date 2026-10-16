#include "paragraph_transport.hpp"
#include "formatted_paragraph.hpp"
#include "formatted_text.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
using namespace eu4unicode;
void check(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
struct Canvas {
    static constexpr int width=512,height=512,margin=32;
    std::vector<unsigned char> alpha=std::vector<unsigned char>(width*height);
    void paint(int x,int y,unsigned w,unsigned h,const std::vector<std::uint8_t>& pixels) {
        check(pixels.size()==static_cast<std::size_t>(w)*h,"transport bitmap retains complete pixels");
        for(unsigned row=0;row<h;++row) for(unsigned column=0;column<w;++column) {
            const auto px=x+static_cast<int>(column)+margin,py=y+static_cast<int>(row)+margin;
            check(px>=0&&py>=0&&px<width&&py<height,"transport ink stays within test canvas");
            auto& target=alpha[py*width+px];
            target=std::max(target,pixels[row*w+column]);
        }
    }
};
}
int main() {
    using namespace eu4unicode;
    const std::string source=u8"§Yسلام§! हिन्दी\nעברית a\u0323\u0301\n";
    auto content=std::make_shared<ParagraphText>(source,true);
    ShapedParagraph paragraph(source,24,220,true,{},content);
    auto plan=plan_paragraph_transport(paragraph,{256,256},1);
    check(paragraph.text()==source&&plan.text!=source,"transport does not alter logical paragraph text");
    Canvas expected,actual;
    for(const auto& tile:rasterize_paragraph(paragraph,254,254))
        expected.paint(static_cast<int>(tile.x),static_cast<int>(tile.y),tile.bitmap.width,tile.bitmap.height,tile.bitmap.alpha);
    int pen=0;std::size_t line=0,seen=0;
    for(std::size_t offset=0;offset<plan.text.size();) {
        const auto unit=native_text_unit(plan.text,offset,true);offset=unit.end;
        if(unit.kind!=TextUnitKind::glyph) continue;
        if(unit.scalar=='\n') {++line;pen=0;continue;}
        check(unit.scalar==0xf0000+seen,"draw tokens address the complete ordered glyph plan");
        const auto& glyph=plan.glyphs.at(seen++);
        actual.paint(pen+glyph.metrics.x_offset,static_cast<int>(paragraph.lines().at(line).top)+glyph.metrics.y_offset,
            glyph.metrics.width,glyph.metrics.height,glyph.alpha);
        pen+=glyph.metrics.advance;
    }
    check(seen==plan.glyphs.size()&&line+1==paragraph.lines().size(),"transport preserves glyph and hard-line counts");
    check(actual.alpha==expected.alpha,"native transport reconstructs the shaped paragraph ink exactly");
    const auto original_plan=plan.text;
    const auto assigned=paragraph_token_start(plan,0xf0100);
    assign_paragraph_tokens(plan,assigned);
    check(assigned==0xf0100&&plan.text.size()==original_plan.size(),"token reassignment retains byte geometry");
    for(std::size_t index=0;index<plan.tokens.size();++index)
        check(decode(std::string_view(plan.text).substr(plan.tokens[index])).value==assigned+index,
            "explicit offsets reassign every generated glyph");
    // The inline bitmap command is a real PUA character. It must not become
    // an alias for a raster token or be rewritten while assigning the plan.
    const auto literal=encode(0xf0000);
    const auto pua_source=u8"العربية "+literal;
    auto pua_content=std::make_shared<ParagraphText>(pua_source,false,
        ParagraphText::IconMeasure{},ParagraphText::ColorLookup{},ParagraphText::IconMeasure{},
        [](std::uint32_t value){return value==0xf0000?13.f:-1.f;});
    ShapedParagraph pua_paragraph(pua_source,24,220,true,{},pua_content);
    auto pua=plan_paragraph_transport(pua_paragraph,{256,256},1);
    const auto pua_first=paragraph_token_start(pua,0xf0000);
    check(pua_first>0xf0000,"transport tokens avoid literal plane-15 bitmap glyphs");
    assign_paragraph_tokens(pua,pua_first);
    std::size_t literals=0;
    for(std::size_t offset=0;offset<pua.text.size();) {
        const auto unit=decode(std::string_view(pua.text).substr(offset));
        if(unit.value==0xf0000) ++literals;
        offset+=unit.bytes;
    }
    check(literals==1,"literal PUA command survives reassignment exactly once");
    bool exhausted=false;
    auto exhausted_plan=plan_paragraph_transport(paragraph,{256,256},1);
    try {paragraph_token_start(exhausted_plan,0xffffd,0xffffe);} catch(const std::length_error&) {exhausted=true;}
    check(exhausted,"insufficient token range fails before packing");
    const std::string mixed=u8"العربية 中 £adm£ हिन्दी @FRA";
    auto objects=std::make_shared<ParagraphText>(mixed,true,[](std::string_view){return 17.f;},
        ParagraphText::ColorLookup{},[](std::string_view){return 23.f;},
        [](std::uint32_t code){return code==0x4e2d?15.f:-1.f;});
    ShapedParagraph inline_paragraph(mixed,24,160,true,{},objects);
    for(const float scale:{.75f,1.f,1.25f,1.5f}) {
        const auto transport=plan_paragraph_transport(inline_paragraph,{256,256},scale);
        std::size_t current_line=0;float pixels=0;std::vector<std::string> commands;
        const auto check_width=[&] {
            check(std::abs(pixels-std::ceil(inline_paragraph.lines().at(current_line).width)*scale)<=1.f,
                "native integer advances retain shaped line width at fractional scales");
        };
        for(std::size_t offset=0;offset<transport.text.size();) {
            const auto unit=native_text_unit(transport.text,offset,true);
            const auto command=transport.text.substr(offset,unit.end-offset);offset=unit.end;
            if(unit.kind==TextUnitKind::color) continue;
            if(unit.kind==TextUnitKind::glyph&&unit.scalar=='\n') {check_width();++current_line;pixels=0;continue;}
            if(unit.kind==TextUnitKind::glyph&&unit.scalar>=0xf0000) {
                pixels+=transport.glyphs.at(unit.scalar-0xf0000).metrics.advance*scale;continue;
            }
            const auto object=std::find_if(objects->icons().begin(),objects->icons().end(),
                [&](const auto& icon){return icon.command==command;});
            check(object!=objects->icons().end(),"native bitmap, icon and flag commands stay in their original renderer");
            commands.push_back(command);pixels+=std::round(object->advance*scale);
        }
        check_width();
        std::sort(commands.begin(),commands.end());
        auto wanted=std::vector<std::string>{u8"中",u8"£adm£","@FRA"};std::sort(wanted.begin(),wanted.end());
        check(commands==wanted,"each native inline object survives transport exactly once");
    }
    for(const auto page:{FontPageSize{2,100},FontPageSize{100,2},FontPageSize{65536,100}}) {
        bool rejected=false;
        try {plan_paragraph_transport(paragraph,page,1);} catch(const std::invalid_argument&) {rejected=true;}
        check(rejected,"invalid native page dimensions fail before atlas allocation");
    }
    std::cout<<"Native paragraph transport ink, commands, hard lines and scaled advances passed.\n";
}
