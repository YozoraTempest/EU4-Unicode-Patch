#include "linux/editor_layout.hpp"
#include "linux/font_pages.hpp"
#include "formatted_paragraph.hpp"
#include "unicode_text.hpp"
#include "unicode_services.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
using namespace eu4unicode;
using namespace eu4unicode::linux_native;
void check(bool condition,const char* message) {
    if(!condition) {std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
template<class T,std::size_t N> void set(std::array<std::byte,N>& target,std::size_t offset,const T& value) {
    std::memcpy(target.data()+offset,&value,sizeof(T));
}
struct Editor {
    std::array<std::byte,0x300> bytes{};
    std::array<std::byte,0x950> font{};
    std::string text;
    explicit Editor(std::string value,float scale=1):text(std::move(value)) {
        const auto source=borrowed_string(text);
        set(bytes,0xc8+0x30,source);set(bytes,0xc8+0x98,static_cast<void*>(font.data()));
        set(font,0x940,scale);
    }
    void* data() {return bytes.data();}
    void* buffer() {return bytes.data()+0xc8;}
    std::uint16_t column() const {
        std::uint16_t value;std::memcpy(&value,bytes.data()+0xc8+0x54,sizeof(value));return value;
    }
};
}
namespace eu4unicode::linux_native {
// Only the atlas/native hook boundary is replaced. The production scope and
// hit adapter run against real Pango/ParagraphText geometry and native fields.
std::shared_ptr<const ShapedParagraph> font_paragraph_layout(void*,std::string_view text,float width,bool wrap,bool formatted,bool force) {
    if(!force&&!needs_native_paragraph_shaping(text,formatted)) return {};
    auto content=std::make_shared<ParagraphText>(text,formatted);
    return std::make_shared<ShapedParagraph>(text,16,width,wrap,nullptr,std::move(content));
}
void log(const char*,...) noexcept {}
void verify_site(std::uintptr_t) {std::abort();}
SafetyHookInline& Hooks::add_inline(std::uintptr_t,void*) {std::abort();}
void Hooks::add_mid(std::uintptr_t,safetyhook::MidHookFn) {std::abort();}
}
int main() {
    Editor editor("A سلام B",1.5f),password("سلام"),plain("中文 English");
    const auto source=borrowed_string(editor.text);
    const int origin[2]={0,0};
    check(!editor_layout_hit(editor.buffer(),&source,origin),"geometry cannot run outside an editor invocation");
    {
        EditorLayoutScope scope(editor.data());
        const auto paragraph=font_paragraph_layout(nullptr,editor.text,32767,false,false,false);
        const auto boundaries=grapheme_boundaries(editor.text);
        bool reversed=false;
        for(std::size_t i=1;i<boundaries.size();++i)
            reversed|=paragraph->caret(boundaries[i]).x<paragraph->caret(boundaries[i-1]).x;
        check(reversed,"fixture contains a real visual/logical ordering reversal");
        bool multiple=false;
        for(const auto first:boundaries) for(const auto last:boundaries) if(first<last) {
            set(editor.bytes,0xc8+0x54,static_cast<std::uint16_t>(first));
            set(editor.bytes,0xc8+0x92,static_cast<std::uint16_t>(last));
            const auto regions=editor_layout_selection(editor.buffer(),editor.text);
            const auto expected=paragraph->selection(first,last);
            check(regions&&regions->size()==expected.size(),"native selection uses every visual region");
            for(std::size_t i=0;i<regions->size();++i) {
                check((*regions)[i].x==std::round(expected[i].x*1.5f),"selection origin follows full-line visual geometry");
                check((*regions)[i].x+(*regions)[i].width==std::round((expected[i].x+expected[i].width)*1.5f),
                    "selection edges retain fractional scale without accumulating width error");
            }
            multiple|=regions->size()>1;
        }
        check(multiple,"fixture exercises disjoint visual selection regions");
        for(int x=0;x<130;++x) {
            const int point[2]={x,0};
            const auto expected=paragraph->hit_test(x/1.5f,0);
            check(editor_layout_hit(editor.buffer(),&source,point),"complex hit uses production geometry adapter");
            check(editor.column()==expected.byte_offset,"native column follows visual hit at fractional font scale");
            check(std::binary_search(boundaries.begin(),boundaries.end(),editor.column()),"native byte offset is a complete grapheme boundary");
            float cross_thread_x=-1;
            std::thread refresh([&] {
                EditorLayoutScope update(editor.data());
                const auto caret=editor_layout_x(editor.buffer(),editor.text);
                check(caret.has_value(),"native refresh has a shaped caret position");
                cross_thread_x=*caret;
            });
            refresh.join();
            check(std::abs(cross_thread_x-std::round(paragraph->caret(expected.byte_offset,expected.trailing).x*1.5f))<.1f,
                "mouse affinity survives a native refresh on another thread");
        }
        const auto column=editor.column();
        const int negative[2]={-1,0};
        check(!editor_layout_hit(editor.buffer(),&source,negative)&&editor.column()==column,"negative point preserves native state");
        set(password.bytes,0x262,true);
        {
            EditorLayoutScope nested(password.data());
            const auto secret=borrowed_string(password.text);
            check(!editor_layout_hit(password.buffer(),&secret,origin),"password text never enters ordinary geometry");
            check(!editor_layout_hit(editor.buffer(),&source,origin),"password scope blocks an outer editor too");
        }
        check(editor_layout_hit(editor.buffer(),&source,origin),"nested scope restores the previous invocation");
        check(!editor_layout_hit(plain.buffer(),&source,origin),"a different buffer cannot borrow the active geometry scope");
    }
    {
        EditorLayoutScope scope(plain.data());
        const auto text=borrowed_string(plain.text);
        check(!editor_layout_hit(plain.buffer(),&text,origin),"ordinary bitmap text retains its existing hit path");
    }
    check(!editor_layout_hit(editor.buffer(),&source,origin),"scope destruction drops the borrowed native pointer");
    check(source.view()==editor.text,"geometry never modifies the logical source string");
    Editor second("A שלום B");
    const int edge[2]={16,0};
    float expected_x=0;
    {
        EditorLayoutScope scope(editor.data());
        editor_layout_hit(editor.buffer(),&source,edge);
        expected_x=*editor_layout_x(editor.buffer(),editor.text);
    }
    {
        EditorLayoutScope scope(second.data());
        const auto text=borrowed_string(second.text);
        editor_layout_hit(second.buffer(),&text,edge);
    }
    {
        EditorLayoutScope scope(editor.data());
        check(*editor_layout_x(editor.buffer(),editor.text)==expected_x,
            "focus switching and another editor's hit preserve the original visual caret");
    }
    forget_editor_layout(second.data());
    forget_editor_layout(editor.data());
    {
        EditorLayoutScope scope(editor.data());
        const auto paragraph=font_paragraph_layout(nullptr,editor.text,32767,false,false,false);
        check(std::abs(*editor_layout_x(editor.buffer(),editor.text)-std::round(paragraph->caret(editor.column()).x*1.5f))<.1f,
            "destroyed editor affinity is not reused");
    }
    std::cout<<"Linux editor geometry scope and visual hit checks passed\n";
}
