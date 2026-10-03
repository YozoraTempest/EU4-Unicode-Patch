#include "scalar_glyph.hpp"
#include "unicode_text.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>

int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=4) throw std::invalid_argument("Usage: scalar_probe FONT_DIRECTORY UTF8_SOURCE OUTPUT_JSON");
        const std::filesystem::path directory(argv[1]);
        auto fonts=std::make_shared<eu4unicode::TextFonts>(std::vector<std::filesystem::path>{directory/L"SourceHanSansSC-Regular.otf",
            directory/L"PlangothicP1-Regular.ttf",directory/L"PlangothicP2-Regular.ttf"});
        std::ifstream source(argv[2],std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(source)),std::istreambuf_iterator<char>());
        if(!source||!eu4unicode::valid_utf8(text)) throw std::invalid_argument("Invalid UTF-8 sample source");
        std::set<std::uint32_t> scalars;
        auto remaining=std::string_view(text);
        while(!remaining.empty()) { const auto scalar=eu4unicode::decode(remaining);if(scalar.value>255) scalars.insert(scalar.value);remaining.remove_prefix(scalar.bytes); }
        std::ofstream out(argv[3]);out<<"{\"size\":16,\"glyphs\":[";bool comma=false;
        for(const auto scalar:scalars) {
            auto glyph=eu4unicode::rasterize_scalar(scalar,16,fonts);
            eu4unicode::TextLayout layout(eu4unicode::encode(scalar),16,100,100,L"Segoe UI",fonts);
            if(comma) out<<',';comma=true;
            out<<"{\"scalar\":"<<scalar<<",\"family\":"<<std::quoted(layout.glyph_runs().at(0).font_family)
                <<",\"metrics\":["<<glyph.metrics.width<<','<<glyph.metrics.height<<','<<glyph.metrics.x_offset<<','<<glyph.metrics.y_offset<<','<<glyph.metrics.advance<<"],\"alpha\":[";
            bool pixel_comma=false;
            for(const auto alpha:glyph.alpha) { if(pixel_comma) out<<',';pixel_comma=true;out<<static_cast<unsigned>(alpha); }
            out<<"]}";
        }
        out<<"]}\n";
        if(!out) throw std::runtime_error("Cannot write scalar raster evidence");
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
