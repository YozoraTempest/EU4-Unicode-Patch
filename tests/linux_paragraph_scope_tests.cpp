#include "linux/paragraph.hpp"
#include "linux/paragraph_fit.hpp"
#include "formatted_paragraph.hpp"
#include "unicode_text.hpp"
#include <array>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {
using namespace eu4unicode;
using namespace eu4unicode::linux_native;
std::shared_ptr<FontParagraph> fixture;
unsigned preparations=0;
void check(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
struct Font {
    alignas(8) std::array<std::byte,0x960> bytes{};
    explicit Font(void* anchor) {
        std::memcpy(bytes.data()+0x100+0x41*8,&anchor,sizeof(anchor));
        const float scale=1;std::memcpy(bytes.data()+0x940,&scale,sizeof(scale));
        const int size=16;std::memcpy(bytes.data()+0x938,&size,sizeof(size));
    }
    void* data() {return bytes.data();}
    void* const* table() const {return reinterpret_cast<void* const*>(bytes.data()+0x100);}
};
}
namespace eu4unicode::linux_native {
// Replace only the platform atlas boundary. The test exercises the actual
// production scope, borrowed String ABI, Pango layout and transport records.
std::shared_ptr<const FontParagraph> font_paragraph_geometry(void*,std::string_view source,float,bool,bool,const ParagraphViewport*) {
    ++preparations;return source=="سلام"?fixture:nullptr;
}
std::shared_ptr<const ShapedParagraph> font_paragraph_layout(void*,std::string_view source,float width,bool wrap,bool formatted,bool force) {
    if(!force&&!needs_native_paragraph_shaping(source,formatted)) return {};
    auto content=std::make_shared<ParagraphText>(source,formatted,
        [](std::string_view){return 12.f;},[](unsigned char){return true;},
        [](std::string_view){return 16.f;},[](std::uint32_t scalar){return scalar<128?8.f:-1.f;});
    return std::make_shared<ShapedParagraph>(source,16,width,wrap,nullptr,std::move(content));
}
void log(const char*,...) noexcept {}
}
int main() {
    using namespace eu4unicode;
    using namespace eu4unicode::linux_native;
    fixture=std::make_shared<FontParagraph>();
    fixture->layout=std::make_shared<ShapedParagraph>("سلام",16,200,false);
    fixture->first_token=0xf0001;fixture->glyphs.resize(2);
    fixture->text=encode(0xf0001)+encode(0xf0002)+encode(0xf0000);
    int anchor=0,other=0;Font font(&anchor),alias(&anchor),different(&other);
    const auto original=borrowed_string("سلام");
    const auto plain=borrowed_string("ordinary");
    const auto* first=&fixture->glyphs[0];
    check(!find_paragraph_glyph(font.table(),0xf0001),"tokens are absent outside a draw invocation");
    {
        ParagraphScope outer(font.data(),&original);
        const auto draw=outer.prepare(&original,200,false,false,"screen");
        check(draw!=&original&&original.view()=="سلام","prepared text never replaces the original owner");
        check(find_paragraph_glyph(font.table(),0xf0001)==first,"active scope resolves generated glyphs");
        check(find_paragraph_glyph(alias.table(),0xf0001)==first,"native font aliases share the active atlas");
        check(!find_paragraph_glyph(different.table(),0xf0001),"another atlas cannot resolve tokens");
        check(!find_paragraph_glyph(font.table(),0xf0000),"literal PUA is outside the generated range");
        const auto copy=std::string(draw->view().substr(4,4));
        const auto copied=borrowed_string(copy);
        {
            const auto before=preparations;
            ParagraphScope inner(alias.data(),&copied);
            check(inner.active()&&inner.prepare(&copied,100,false,false,"actual")->view()==copy,
                "a copied native draw substring inherits only its parent geometry");
            check(preparations==before,"inherited transport is never shaped again");
            check(find_paragraph_glyph(alias.table(),0xf0002)==&fixture->glyphs[1],"copied draw input retains token lookup");
        }
        {
            ParagraphScope inner(font.data(),&plain);
            check(!inner.active()&&!find_paragraph_glyph(font.table(),0xf0001),"nested ordinary text masks parent tokens");
        }
        check(find_paragraph_glyph(font.table(),0xf0001)==first,"parent resumes after a nested call");
        {
            ParagraphScope inner(different.data(),draw);
            check(!inner.active(),"a matching string from another atlas cannot inherit transport");
        }
        {
            ParagraphScope inner(nullptr,nullptr);
            check(!find_paragraph_glyph(font.table(),0xf0001),"non-font scopes explicitly mask transport");
        }
    }
    check(!find_paragraph_glyph(font.table(),0xf0001)&&fixture.use_count()==1,"scope exit releases geometry and all token visibility");
    {
        const std::string source="前缀测试后缀";
        const FormattedText boundaries(source,false);
        std::size_t begin=4,end=10;
        align_font_substring(boundaries,begin,end);
        check(begin==3&&end==9&&source.substr(begin,end-begin)=="缀测",
            "native substring aligns two absolute positions, including a nonzero start");
    }
    const unsigned padding[]{3,2};
    const std::string source="§Gسلام हिन्दी e\xcc\x81§! @FRA\nשלום 中文\nآخر";
    const auto full=fit_paragraph(font.data(),source,240,200,padding,true,false,true);
    check(full&&full->text==source&&!full->clipped&&full->height==52,
        "full logical text, native grid height and two vertical margins are retained");
    const auto clipped=fit_paragraph(font.data(),source,240,20,padding,true,false,true);
    check(clipped&&clipped->clipped&&clipped->ellipsis&&clipped->text.ends_with(" ...")&&clipped->height==36,
        "height clipping retains the native attempted-line height and ellipsis");
    check(valid_utf8(clipped->text)&&clipped->text.find("שלום")==std::string::npos&&
        clipped->text.find(encode(0xf0001))==std::string::npos,
        "fitted owners receive valid logical source, with no later lines or draw tokens");
    const auto truncated=fit_paragraph(font.data(),source,240,20,padding,true,false,false);
    check(truncated&&truncated->text==source.substr(0,source.find('\n'))&&!truncated->ellipsis,
        "truncate mode removes complete later lines without adding an ellipsis");
    const auto wrapped=fit_paragraph(font.data(),source,70,200,padding,true,true,true);
    check(wrapped&&wrapped->text.find('\n')!=std::string::npos&&valid_utf8(wrapped->text)&&!wrapped->ellipsis,
        "native wrap-output mode inserts logical line breaks and never draw tokens");
    const auto small=fit_paragraph(font.data(),source,40,20,padding,true,false,true);
    check(small&&small->clipped&&valid_utf8(small->text),"narrow clipping never splits a combining sequence");
    const auto fitted=font_paragraph_layout(font.data(),small->text,34,true,true,true);
    check(fitted->lines().size()==1&&plan_paragraph_line(*fitted,0,1).pixels<=34,
        "the ellipsized logical source is re-shaped to fit the original content width");
    const auto ordinary=fit_paragraph(font.data(),"ordinary 中文",240,20,padding,true,false,true);
    check(!ordinary,"ordinary bitmap fitting remains with the original engine");
    std::cout<<"Linux paragraph scope nesting, native aliases, copied input, PUA isolation and lifetime passed.\n";
}
