#include "unicode_layout.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
struct Glyph {
    std::uint32_t scalar;
    int x=0,y=0,width=1,height=1,x_offset=0,y_offset=0,advance=0;
    std::vector<std::uint8_t> alpha;
};
void pack(const std::set<std::uint32_t>& scalars,const std::filesystem::path& destination,int size) {
    constexpr int atlas_width=1024;
    const int base=static_cast<int>(std::round(size*0.8));
    std::vector<Glyph> glyphs;
    int x=1,y=1,row_height=0;
    std::size_t missing=0;
    for(const auto scalar:scalars) {
        const auto text=eu4unicode::encode(scalar);
        eu4unicode::TextLayout layout(text,static_cast<float>(size),512,512,L"Microsoft YaHei UI");
        for(const auto& run:layout.glyph_runs()) missing+=std::count(run.glyphs.begin(),run.glyphs.end(),0);
        const auto image=layout.rasterize();
        int left=static_cast<int>(image.width),top=static_cast<int>(image.height),right=-1,bottom=-1;
        for(std::uint32_t iy=0;iy<image.height;++iy) for(std::uint32_t ix=0;ix<image.width;++ix) {
            if(image.pixels[(static_cast<std::size_t>(iy)*image.width+ix)*4+3]) {
                left=(std::min)(left,static_cast<int>(ix)); top=(std::min)(top,static_cast<int>(iy));
                right=(std::max)(right,static_cast<int>(ix)); bottom=(std::max)(bottom,static_cast<int>(iy));
            }
        }
        Glyph glyph{scalar};
        glyph.advance=static_cast<int>(std::ceil(layout.metrics().width));
        if(right>=left) {
            glyph.width=right-left+1; glyph.height=bottom-top+1;
            glyph.x_offset=left-16;
            glyph.y_offset=base-static_cast<int>(std::round(image.baseline))+top-16;
            glyph.alpha.resize(static_cast<std::size_t>(glyph.width)*glyph.height);
            for(int iy=0;iy<glyph.height;++iy) for(int ix=0;ix<glyph.width;++ix)
                glyph.alpha[static_cast<std::size_t>(iy)*glyph.width+ix]=image.pixels[(static_cast<std::size_t>(top+iy)*image.width+left+ix)*4+3];
        } else glyph.alpha={0};
        if(glyph.width+2>atlas_width) throw std::runtime_error("Glyph is wider than the atlas");
        if(x+glyph.width+1>atlas_width) { x=1; y+=row_height+1; row_height=0; }
        glyph.x=x; glyph.y=y; x+=glyph.width+1; row_height=(std::max)(row_height,glyph.height);
        glyphs.push_back(std::move(glyph));
    }
    int atlas_height=1;
    while(atlas_height<y+row_height+1) atlas_height*=2;
    if(atlas_height>8192) throw std::runtime_error("Font exceeds the single-atlas research budget");
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(atlas_width)*atlas_height*4,size==88?0:255);
    for(std::size_t index=3;index<pixels.size();index+=4) pixels[index]=0;
    for(const auto& glyph:glyphs) for(int iy=0;iy<glyph.height;++iy) for(int ix=0;ix<glyph.width;++ix)
        pixels[(static_cast<std::size_t>(glyph.y+iy)*atlas_width+glyph.x+ix)*4+3]=glyph.alpha[static_cast<std::size_t>(iy)*glyph.width+ix];
    // UI masks are white; the map shader uses the atlas RGB as label color.
    // DDS stores uncompressed A8R8G8B8 with one mip level.
    std::array<std::uint32_t,32> header{};
    header[0]=0x20534444; header[1]=124; header[2]=0x100f;
    header[3]=atlas_height; header[4]=atlas_width; header[5]=atlas_width*4;
    header[19]=32; header[20]=0x41; header[22]=32;
    header[23]=0x00ff0000; header[24]=0x0000ff00; header[25]=0x000000ff; header[26]=0xff000000; header[27]=0x1000;
    const auto name=size==88?"zh-hans-map":"zh-hans-"+std::to_string(size);
    std::ofstream texture(destination/(name+".dds"),std::ios::binary);
    texture.write(reinterpret_cast<const char*>(header.data()),sizeof(header));
    texture.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));
    if(!texture) throw std::runtime_error("Cannot write font texture");
    std::ofstream font(destination/(name+".fnt"));
    font<<"info face=\"DirectWrite system fallback\" size="<<size<<" bold=0 italic=0 charset=\"\" stretchH=100 smooth=1 aa=1 padding=0,0,0,0 spacing=1,1\n";
    font<<"common lineHeight="<<size<<" base="<<base<<" scaleW="<<atlas_width<<" scaleH="<<atlas_height<<" pages=1\n";
    for(const auto& glyph:glyphs)
        font<<"char id="<<glyph.scalar<<" x="<<glyph.x<<" y="<<glyph.y<<" width="<<glyph.width<<" height="<<glyph.height
            <<" xoffset="<<glyph.x_offset<<" yoffset="<<glyph.y_offset<<" xadvance="<<glyph.advance<<" page=0\n";
    if(!font) throw std::runtime_error("Cannot write font metrics");
    std::cout<<name<<": "<<glyphs.size()<<" glyphs, "<<atlas_width<<'x'<<atlas_height<<", missing="<<missing<<'\n';
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=3) throw std::invalid_argument("Usage: fontpack UTF8_SOURCE OUTPUT_DIRECTORY");
        std::ifstream source(argv[1],std::ios::binary);
        if(!source) throw std::runtime_error("Cannot read font character source");
        std::string content((std::istreambuf_iterator<char>(source)),std::istreambuf_iterator<char>());
        if(!eu4unicode::valid_utf8(content)) throw std::invalid_argument("Font character source must be UTF-8");
        std::set<std::uint32_t> scalars;
        for(std::uint32_t scalar=0x20;scalar<=0xff;++scalar) if(scalar<0x7f||scalar>=0xa0) scalars.insert(scalar);
        scalars.insert(0x2026);
        auto remaining=std::string_view(content);
        while(!remaining.empty()) {
            const auto scalar=eu4unicode::decode(remaining);
            if(scalar.value>=0x20&&scalar.value!=0xfeff&&!(scalar.value>=0x7f&&scalar.value<0xa0)) scalars.insert(scalar.value);
            remaining.remove_prefix(scalar.bytes);
        }
        std::filesystem::create_directories(argv[2]);
        for(const auto size:{14,16,18,24,88}) pack(scalars,argv[2],size);
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
