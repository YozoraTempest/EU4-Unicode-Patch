#include "unicode_layout.hpp"
#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <limits>
#include <fstream>
#include <iomanip>
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
        check(run.face&&run.em_size>0&&std::isfinite(run.baseline_x)&&std::isfinite(run.baseline_y),
            "glyph run retains the exact face, size and visual baseline");
        check(run.offsets.size()==run.glyphs.size()&&run.advances.size()==run.glyphs.size(),
            "every shaped glyph has advance and offset geometry");
        auto source_offset=run.text_start;
        std::vector<bool> mapped(run.glyphs.size(),false);
        for(const auto& cluster:run.clusters) {
            check(cluster.text_start==source_offset&&cluster.text_length>0,
                "clusters cover the run in logical source order");
            check(valid_utf8(std::string_view(text).substr(cluster.text_start,cluster.text_length)),
                "cluster source ranges contain complete UTF-8 scalars");
            check(cluster.glyph_count>0&&cluster.first_glyph+cluster.glyph_count<=run.glyphs.size(),
                "cluster glyph ranges stay inside the shaped run");
            for(auto index=cluster.first_glyph;index<cluster.first_glyph+cluster.glyph_count;++index) {
                check(!mapped[index],"shaped glyph belongs to exactly one source cluster");mapped[index]=true;
            }
            source_offset+=cluster.text_length;
        }
        check(source_offset==run.text_start+run.text_length&&
            std::all_of(mapped.begin(),mapped.end(),[](bool value){return value;}),
            "cluster mapping covers all source bytes and shaped glyphs");
    }
    check(rtl,"Arabic and Hebrew produce RTL runs");
    check(fonts.size()>1,"system font fallback selects multiple font families");
    // The temporary layout is gone before the copied runs are rasterized.
    const std::string detached_text=u8"ffi a\u0323\u0301 العربية क्षि 𠀀";
    const auto detached=TextLayout(detached_text,28,600,100).glyph_runs();
    bool joined_cluster=false,rtl_ink=false,gray_alpha=false,blank_advance=false;
    for(const auto& run:detached) {
        const auto raster=rasterize_glyph_run(run);
        check(raster.alpha.size()==static_cast<std::size_t>(raster.width)*raster.height,
            "retained shaped runs rasterize after the source layout is destroyed");
        const auto source=std::string_view(detached_text).substr(run.text_start,run.text_length);
        if(std::all_of(source.begin(),source.end(),[](char value){return value==' ';})) {
            check(raster.alpha.empty()&&run.advances[0]>0,"blank run retains advance without allocating an ink bitmap");
            blank_advance=true;
        } else {
            check(std::any_of(raster.alpha.begin(),raster.alpha.end(),[](auto value){return value!=0;}),
                "retained exact face produces shaped ink");
        }
        rtl_ink=rtl_ink||((run.bidi_level%2)!=0&&raster.left<0);
        gray_alpha=gray_alpha||std::any_of(raster.alpha.begin(),raster.alpha.end(),[](auto value){return value>0&&value<255;});
        for(const auto& cluster:run.clusters) {
            const auto value=std::string_view(detached_text).substr(cluster.text_start,cluster.text_length);
            joined_cluster=joined_cluster||decode(value).bytes<value.size();
        }
    }
    check(joined_cluster&&rtl_ink&&gray_alpha&&blank_advance,
        "ligature/mark clusters, RTL bounds, grayscale coverage and blank advances are retained");
    const auto original=rasterize_glyph_run(detached.front());
    auto shifted=detached.front();
    for(auto& offset:shifted.offsets) { offset.advance+=5;offset.ascender+=3; }
    const auto moved=rasterize_glyph_run(shifted);
    check(moved.left==original.left+5&&moved.top==original.top-3&&moved.width==original.width&&
        moved.height==original.height&&moved.alpha==original.alpha,
        "rasterization uses shaped offsets rather than reconstructing text");
    auto translated=detached.front();translated.baseline_x+=17;translated.baseline_y+=9;
    const auto integer_moved=rasterize_glyph_run(translated);
    check(integer_moved.left==original.left&&integer_moved.top==original.top&&
        integer_moved.width==original.width&&integer_moved.height==original.height&&integer_moved.alpha==original.alpha,
        "integer baseline translation preserves local atlas pixels");
    translated.baseline_x+=.25f;
    const auto phased=rasterize_glyph_run(translated);
    check(phased.alpha!=original.alpha,"fractional baseline phase affects raster coverage");
    bool incomplete=false,nonfinite=false;
    try { auto invalid=detached.front();invalid.offsets.pop_back();rasterize_glyph_run(invalid); }
    catch(const std::invalid_argument&) { incomplete=true; }
    try { auto invalid=detached.front();invalid.advances[0]=std::numeric_limits<float>::infinity();rasterize_glyph_run(invalid); }
    catch(const std::invalid_argument&) { nonfinite=true; }
    check(incomplete&&nonfinite,"rasterizer rejects incomplete and nonfinite glyph geometry");
    bool budget=false,invalid_baseline=false,missing_face=false;
    try { auto invalid=detached.front();invalid.em_size=20000;rasterize_glyph_run(invalid); }
    catch(const std::length_error&) { budget=true; }
    try { auto invalid=detached.front();invalid.baseline_x=std::numeric_limits<float>::quiet_NaN();rasterize_glyph_run(invalid); }
    catch(const std::invalid_argument&) { invalid_baseline=true; }
    try { auto invalid=detached.front();invalid.face.reset();rasterize_glyph_run(invalid); }
    catch(const std::invalid_argument&) { missing_face=true; }
    check(budget&&invalid_baseline&&missing_face,"rasterizer rejects excessive size, invalid baseline and missing font ownership");
    const auto bitmap=TextLayout(u8"𠀀",28,100,100).rasterize();
    check(bitmap.pixels.size()==static_cast<std::size_t>(bitmap.width)*bitmap.height*4&&bitmap.baseline>0,
        "raster dimensions and baseline are available to the native atlas bridge");
    bool ink=false,transparent=false;
    for(std::size_t offset=3;offset<bitmap.pixels.size();offset+=4) {
        ink=ink||bitmap.pixels[offset]!=0;
        transparent=transparent||bitmap.pixels[offset]==0;
    }
    check(ink&&transparent,"supplementary glyph raster contains ink and transparent background");
    const auto boundaries=grapheme_boundaries(text);
    for(float y=0;y<measured.height;y+=11) for(float x=-10;x<measured.width+10;x+=9) {
        const auto hit=layout.hit_test(x,y);
        check(std::binary_search(boundaries.begin(),boundaries.end(),hit.byte_offset),"hit testing selects a complete UTF-8 grapheme");
    }
    bool rejected=false;
    try { TextLayout bad("\xed\xa0\x80",20,100,100); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"renderer rejects malformed UTF-8");
    if(argc==2) layout.render_png(argv[1]);
    if(argc==3) {
        layout.render_png(argv[1]);
        const std::filesystem::path directory=argv[2];
        std::filesystem::create_directories(directory);
        std::ofstream manifest(directory/"runs.tsv",std::ios::binary);
        manifest<<std::setprecision(9);
        manifest<<"index\tbaseline_x\tbaseline_y\tleft\ttop\twidth\theight\tbidi\ttext_start\ttext_length\tfamily\n";
        for(std::size_t index=0;index<runs.size();++index) {
            const auto& run=runs[index];const auto image=rasterize_glyph_run(run);
            manifest<<index<<'\t'<<run.baseline_x<<'\t'<<run.baseline_y<<'\t'<<image.left<<'\t'<<image.top<<'\t'
                <<image.width<<'\t'<<image.height<<'\t'<<run.bidi_level<<'\t'<<run.text_start<<'\t'<<run.text_length
                <<'\t'<<run.font_family<<'\n';
            if(image.alpha.empty()) continue;
            std::ofstream mask(directory/("run-"+std::to_string(index)+".pgm"),std::ios::binary);
            mask<<"P5\n"<<image.width<<' '<<image.height<<"\n255\n";
            mask.write(reinterpret_cast<const char*>(image.alpha.data()),static_cast<std::streamsize>(image.alpha.size()));
            check(static_cast<bool>(mask),"glyph raster diagnostic write succeeds");
        }
        check(static_cast<bool>(manifest),"glyph run diagnostic manifest write succeeds");
    }
    std::cout<<"DirectWrite layout, fallback, bidi and hit-test checks passed. Missing glyphs: "<<missing<<"\n";
    for(const auto& family:fonts) std::cout<<family<<'\n';
}
