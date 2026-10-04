#include "native_paragraph.hpp"
#include "native_font_atlas.hpp"
#include "unicode_text.hpp"
#include "formatted_text.hpp"
#include "formatted_paragraph.hpp"
#include <windows.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void check(bool value,const char* message) {
    if(!value) { std::cerr<<"FAIL: "<<message<<'\n';std::exit(1); }
}
namespace {
using namespace eu4unicode;
int icon_width(void*,const char* name) { return std::strcmp(name,"adm")==0?18:0; }
bool color_lookup(void*,unsigned char code,std::uint32_t* value) {
    *value=0xffffffff;return code=='Y'||code=='R'||code=='G'||code=='!';
}
struct Font {
    alignas(void*) std::array<std::byte,0x4000> object{};
    alignas(void*) std::array<std::byte,0x500> context{};
    NativeGlyph anchor{};
    std::array<void*,32> methods{};
    template<class T> void put(std::size_t offset,T value) { std::memcpy(object.data()+offset,&value,sizeof(value)); }
    Font(void* manager) {
        methods[0xe8/8]=reinterpret_cast<void*>(icon_width);put(0,methods.data());
        put(0x48,context.data());put(0x120+0x41*8,&anchor);put(0x960,18);put(0x968,1.f);
        put(0x970,7);put(0x978,2048);put(0x97c,4096);
        std::memcpy(context.data()+0x480,&manager,sizeof(manager));
    }
    void* data() { return object.data(); }
    void* const* table() const { return reinterpret_cast<void* const*>(object.data()+0x120); }
};
EngineString borrow(const std::string& text) {
    EngineString value{};value.storage.pointer=text.c_str();value.size=text.size();value.capacity=(std::max)(std::uint64_t{16},value.size);return value;
}
std::uint32_t probe_token=0;
int native_width(void* font,const char*,int,bool) {
    const auto table=reinterpret_cast<void* const*>(static_cast<const std::byte*>(font)+0x120);
    return find_paragraph_glyph(table,probe_token)?1000:-42;
}
int native_height(void*,const EngineString*,int,int,const int*,bool) { return -43; }
int transport_width(void* font,const char* source,int length,bool formatted) {
    const auto text=std::string_view(source,length<0?std::strlen(source):static_cast<std::size_t>(length));
    const auto table=reinterpret_cast<void* const*>(static_cast<const std::byte*>(font)+0x120);
    const auto scale=*reinterpret_cast<const float*>(static_cast<const std::byte*>(font)+0x968);
    float width=0,maximum=0;
    for(std::size_t offset=0;offset<text.size();) {
        const auto unit=native_text_unit(text,offset,formatted);offset=unit.end;
        if(unit.kind==TextUnitKind::color) continue;
        if(unit.kind==TextUnitKind::icon) { width+=18;continue; }
        if(unit.scalar=='\n') { maximum=(std::max)(maximum,width);width=0;continue; }
        const auto glyph=find_paragraph_glyph(table,unit.scalar);
        check(glyph!=nullptr,"formatted transport measurement uses the active paragraph glyphs");
        width+=glyph->advance*scale;
    }
    return static_cast<int>((std::max)(maximum,width));
}
}
int main() {
    using namespace eu4unicode;
    const auto directory=std::filesystem::current_path()/("native-paragraph-fixture-"+std::to_string(GetCurrentProcessId()));
    check(std::filesystem::create_directory(directory),"isolated fixture directory is newly created");
    std::filesystem::create_directory(directory/"gfx");std::filesystem::create_directory(directory/"gfx/fonts");
    std::ofstream metrics(directory/"gfx/fonts/zh-hans-18.fnt");
    metrics<<"common lineHeight=18 scaleW=2048 scaleH=4096\nchar id=65 x=1 y=4080 width=7 height=10\n";metrics.close();
    configure_font_atlases(directory,directory/"no-optional-fonts",nullptr,"gfx/fonts/",true);
    native_paragraph_color=color_lookup;
    int manager=0;
    Font font(&manager),alias(&manager),other(nullptr);
    register_font_atlas(font.data(),"gfx/fonts/zh-hans-18");
    register_font_atlas(alias.data(),"gfx/fonts/zh-hans-18");
    check(dynamic_font(font.data()),"generated native UI font is bound without a graphics device");
    const std::string text=u8"العربية 123 English\nالعربية";
    const auto geometry=font_paragraph_geometry(font.data(),text,200,true);
    check(geometry&&geometry->layout->text()==text,"original paragraph accompanies its native geometry");
    check(geometry==font_paragraph_geometry(font.data(),text,200,true),"repeated layout reuses stable tokens and glyph records");
    check(geometry==font_paragraph_geometry(alias.data(),text,200,true),"texture aliases share shaped paragraph geometry");
    const auto decoded=geometry->draw_text;
    std::size_t index=0,line=0;int advance=0;
    bool paged=false;
    for(std::size_t offset=0;offset<decoded.size();) {
        const auto scalar=decode(std::string_view(decoded).substr(offset));
        check(scalar.valid,"native draw transport contains complete UTF-8 units");offset+=scalar.bytes;
        if(scalar.value=='\n') {
            check(advance==static_cast<int>(std::ceil(geometry->layout->lines()[line].width)),"line advance comes from the shaped paragraph");
            ++line;advance=0;continue;
        }
        check(scalar.value==geometry->first_token+index,"native transport tokens are stable and scoped");
        const auto glyph=geometry->glyphs[index++];advance+=glyph->advance;
        check(glyph->kerning==0,"native byte kerning does not reshape contextual runs");
        check(!find_unicode_glyph(font.table(),scalar.value),"transport records do not occupy Unicode scalar registry entries");
        paged=paged||font_glyph_page(glyph)>0;
    }
    check(index==geometry->glyphs.size()&&paged,"shaped runs use the existing additional texture pages");
    check(advance==static_cast<int>(std::ceil(geometry->layout->lines()[line].width)),"final line advance matches its geometry");
    auto source=borrow(text);const int box[4]={0,0,200,100};
    const auto draw=begin_native_paragraph(font.data(),&source,box,0);
    check(draw!=&source&&std::string_view(draw->data(),draw->size)==geometry->draw_text,"main UI invocation receives the borrowed draw transport");
    check(std::string_view(source.data(),source.size)==text,"engine source text is unchanged");
    check(find_paragraph_glyph(font.table(),geometry->first_token)==geometry->glyphs.front(),"active invocation resolves contextual glyph records");
    original_text_width=native_width;original_text_height=native_height;probe_token=geometry->first_token;
    check(measure_paragraph_text(font.data(),draw->data(),static_cast<int>(draw->size),true)==1000,
        "native transport measurement uses scoped glyph advances without reshaping its tokens");
    check(measure_paragraph_text(font.data(),"Latin",5,true)==-42,
        "unrelated native measurement cannot consume its parent's transport glyphs");
    check(find_paragraph_glyph(font.table(),geometry->first_token)==geometry->glyphs.front(),
        "unrelated measurement restores the active draw scope");
    check(!find_paragraph_glyph(other.table(),geometry->first_token),"another font cannot resolve scoped transport tokens");
    const std::string latin="Latin";auto ordinary=borrow(latin);
    check(begin_native_paragraph(font.data(),&ordinary,box,0)==&ordinary,"ordinary nested text keeps native metrics");
    check(!find_paragraph_glyph(font.table(),geometry->first_token),"ordinary nested invocation masks its parent scope");
    end_native_paragraph();
    check(find_paragraph_glyph(font.table(),geometry->first_token)==geometry->glyphs.front(),"parent paragraph scope is restored");
    end_native_paragraph();check(!find_paragraph_glyph(font.table(),geometry->first_token),"tokens cease resolving after the draw returns");
    for(int depth=0;depth<35;++depth) begin_native_paragraph(font.data(),&source,box,0);
    check(!find_paragraph_glyph(font.table(),geometry->first_token),"scope capacity overflow cannot expose a parent paragraph");
    for(int depth=0;depth<35;++depth) end_native_paragraph();
    check(!find_paragraph_glyph(font.table(),geometry->first_token),"deep nesting unwinds without a stale scope");
    original_text_width=native_width;original_text_height=native_height;
    const auto unwrapped=font_paragraph_layout(font.data(),text,32767,false);
    check(measure_paragraph_text(font.data(),text.c_str(),static_cast<int>(text.size()),true)==
        static_cast<int>(std::ceil(unwrapped->metrics().width)),"public native width uses the shared paragraph metrics");
    check(measure_paragraph_text(font.data(),text.c_str(),-1,false)==
        static_cast<int>(std::ceil(unwrapped->metrics().width)),"NUL-terminated native width uses the same layout");
    const int margin[4]={0,0,0,0};
    check(measure_paragraph_height(font.data(),&source,200,100,margin,true)==
        static_cast<int>(geometry->layout->metrics().lines)*18,"public native height shares wrapped line metrics");
    check(measure_paragraph_text(font.data(),latin.c_str(),-1,true)==-42,"ordinary native width keeps its existing contract");
    const std::string formatted=u8"§Yالعربية§!";auto colored=borrow(formatted);
    const auto colored_layout=font_paragraph_layout(font.data(),formatted,32767,false);
    check(measure_paragraph_text(font.data(),formatted.c_str(),-1,true)==
        static_cast<int>(plan_paragraph_line(*colored_layout,0,1).pixels),"formatted width uses the contextual paragraph");
    check(begin_native_paragraph(font.data(),&colored,box,0)!=&colored,"formatted draws receive the scoped paragraph transport");
    end_native_paragraph();
    std::string nested;
    for(int depth=0;depth<41;++depth) nested+=u8"§Y";
    nested+=u8"العربية";
    bool capacity_rejected=false;
    try { font_paragraph_geometry(font.data(),nested,400,false); }
    catch(const std::length_error&) { capacity_rejected=true; }
    check(capacity_rejected,"final color resets count toward the native line buffer limit");
    const std::string mixed=u8"§Yالعربية §R123 £adm£§! हिन्दी§! £adm£";
    auto rich_source=borrow(mixed);
    for(const auto scale:{1.f,1.1f,1.5f}) {
        font.put(0x968,scale);
        const auto layout=font_paragraph_layout(font.data(),mixed,200.f/scale,true);
        const auto rich_geometry=font_paragraph_geometry(font.data(),mixed,200.f/scale,true);
        check(layout==rich_geometry->layout&&layout->objects().size()==2,"measurement and geometry share native inline object layout");
        original_text_width=transport_width;
        float expected=0;
        for(std::size_t line=0;line<layout->lines().size();++line)
            expected=(std::max)(expected,plan_paragraph_line(*layout,line,scale).pixels);
        const auto rich_draw=begin_native_paragraph(font.data(),&rich_source,box,0);
        check(rich_draw!=&rich_source&&measure_paragraph_text(font.data(),rich_draw->data(),static_cast<int>(rich_draw->size),true)==
            static_cast<int>(expected),"decorated transport keeps the shaped line advances at fractional UI scales");
        check(std::string_view(rich_source.data(),rich_source.size)==mixed,"decorated source strings remain unchanged");
        end_native_paragraph();
        const auto button=begin_native_button_paragraph(font.data(),&rich_source,200,margin,true);
        check(button!=&rich_source,"button draw scope uses the formatted paragraph");end_native_paragraph();
        const auto popup=begin_native_popup_paragraph(font.data(),&rich_source,200);
        check(popup!=&rich_source,"popup draw scope uses the formatted paragraph");end_native_paragraph();
        const auto unbounded=begin_native_popup_paragraph(font.data(),&rich_source,-1);
        check(unbounded!=&rich_source&&std::string_view(unbounded->data(),unbounded->size).find('\n')==std::string_view::npos,
            "negative popup widths retain the native no-wrap contract");end_native_paragraph();
        const auto full=font_paragraph_layout(font.data(),mixed,32767,false);
        float full_width=0;
        for(std::size_t line=0;line<full->lines().size();++line)
            full_width=(std::max)(full_width,plan_paragraph_line(*full,line,scale).pixels);
        check(measure_paragraph_text(font.data(),mixed.c_str(),-1,true)==static_cast<int>(full_width),
            "public width includes original native icon advances");
    }
    original_text_width=native_width;
    font.put(0x968,1.5f);
    check(measure_paragraph_text(font.data(),text.c_str(),-1,true)==
        static_cast<int>(std::ceil(unwrapped->metrics().width)*1.5f),"native scaling follows the shaped integer advance");
    const auto page=font_glyph_page(geometry->glyphs.front());
    release_font_atlas(font.table());
    check(dynamic_font(alias.data())&&font_glyph_page(geometry->glyphs.front())==page,"shaped pages survive an independent font alias release");
    release_font_atlas(alias.table());
    check(!dynamic_font(alias.data())&&font_glyph_page(geometry->glyphs.front())==0,"last atlas release clears shaped page ownership");
    std::filesystem::remove_all(directory);
    std::cout<<"Native paragraph metrics, scoped transport, page ownership and source preservation checks passed.\n";
}
