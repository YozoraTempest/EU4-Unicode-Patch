#include "linux/paragraph.hpp"
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
    }
    void* data() {return bytes.data();}
    void* const* table() const {return reinterpret_cast<void* const*>(bytes.data()+0x100);}
};
}
namespace eu4unicode::linux_native {
// Replace only the platform atlas boundary. The test exercises the actual
// production scope, borrowed String ABI, Pango layout and transport records.
std::shared_ptr<const FontParagraph> font_paragraph_geometry(void*,std::string_view source,float,bool,bool) {
    ++preparations;return source==u8"سلام"?fixture:nullptr;
}
void log(const char*,...) noexcept {}
}
int main() {
    using namespace eu4unicode;
    using namespace eu4unicode::linux_native;
    fixture=std::make_shared<FontParagraph>();
    fixture->layout=std::make_shared<ShapedParagraph>(u8"سلام",16,200,false);
    fixture->first_token=0xf0001;fixture->glyphs.resize(2);
    fixture->text=encode(0xf0001)+encode(0xf0002)+encode(0xf0000);
    int anchor=0,other=0;Font font(&anchor),alias(&anchor),different(&other);
    const auto original=borrowed_string(u8"سلام");
    const auto plain=borrowed_string("ordinary");
    const auto* first=&fixture->glyphs[0];
    check(!find_paragraph_glyph(font.table(),0xf0001),"tokens are absent outside a draw invocation");
    {
        ParagraphScope outer(font.data(),&original);
        const auto draw=outer.prepare(&original,200,false,false,"screen");
        check(draw!=&original&&original.view()==u8"سلام","prepared text never replaces the original owner");
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
    std::cout<<"Linux paragraph scope nesting, native aliases, copied input, PUA isolation and lifetime passed.\n";
}
