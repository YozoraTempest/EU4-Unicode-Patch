#include "scalar_glyph.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=3) throw std::invalid_argument("Usage: open_font_tests FONT_DIRECTORY OUTPUT_PNG");
        const std::filesystem::path directory(argv[1]);
        auto fonts=std::make_shared<eu4unicode::TextFonts>(std::vector<std::filesystem::path>{
            directory/L"SourceHanSansSC-Regular.otf",directory/L"PlangothicP1-Regular.ttf",directory/L"PlangothicP2-Regular.ttf"});
        const std::vector<std::string> expected{"Source Han Sans SC","Plangothic P1","Plangothic P2"};
        if(fonts->families()!=expected) throw std::runtime_error("Unexpected pinned font family order");
        std::string text="常用汉字：中华人民共和国 欧洲风云 外交搜索\n";
        for(const auto& sample:std::vector<std::pair<std::uint32_t,const char*>>{
            {0x3400,"Source Han Sans SC"},{0x4e00,"Source Han Sans SC"},{0x9fff,"Source Han Sans SC"},
            {0x20000,"Plangothic P1"},{0x2a700,"Plangothic P1"},{0x2ebf0,"Plangothic P1"},
            {0x30000,"Plangothic P2"},{0x31350,"Plangothic P2"},{0x323b0,"Plangothic P2"}}) {
            const auto scalar=eu4unicode::encode(sample.first);
            eu4unicode::TextLayout layout(scalar,16,100,100,L"Segoe UI",fonts);
            const auto runs=layout.glyph_runs();
            if(runs.size()!=1||runs[0].font_family!=sample.second||runs[0].glyphs.empty()||
               std::find(runs[0].glyphs.begin(),runs[0].glyphs.end(),0)!=runs[0].glyphs.end())
                throw std::runtime_error("File-backed font fallback selected an incorrect face");
            const auto glyph=eu4unicode::rasterize_scalar(sample.first,16,fonts);
            if(glyph.metrics.advance<=0||glyph.metrics.width<=1||glyph.metrics.height<=1||
               std::none_of(glyph.alpha.begin(),glyph.alpha.end(),[](auto value){return value!=0;}))
                throw std::runtime_error("Unicode glyph produced empty native metrics or pixels");
            text+=scalar+" ";
            std::cout<<"U+"<<std::hex<<sample.first<<std::dec<<": "<<runs[0].font_family<<" advance="<<glyph.metrics.advance<<'\n';
        }
        // A retained run remains usable after the file collection and layout
        // have been destroyed; the exact file-backed face owns its resources.
        eu4unicode::GlyphRun retained;
        { eu4unicode::TextLayout layout(eu4unicode::encode(0x323b0),32,100,100,L"Segoe UI",fonts); retained=layout.glyph_runs().at(0); }
        { eu4unicode::TextLayout preview(text,24,900,300,L"Segoe UI",fonts);preview.render_png(argv[2]); }
        fonts.reset();
        const auto bitmap=eu4unicode::rasterize_glyph_run(retained);
        if(bitmap.alpha.empty()) throw std::runtime_error("Retained open font run has lost its face");
        std::cout<<"9 file-backed fallback/raster samples and retained face passed.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
