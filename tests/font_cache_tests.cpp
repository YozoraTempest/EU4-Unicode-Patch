#include "font_atlas_assets.hpp"
#include "scalar_glyph.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
std::map<std::string,int> properties(const std::string& line) {
    std::istringstream source(line);std::string word;
    std::map<std::string,int> result;
    while(source>>word) {
        const auto equal=word.find('=');
        if(equal!=std::string::npos) result.emplace(word.substr(0,equal),std::stoi(word.substr(equal+1)));
    }
    return result;
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        require(argc==2,"Usage: font_cache_tests OUTPUT_DIRECTORY");
        const std::filesystem::path root(argv[1]);
        for(const auto size:{14,16,18,24,88}) {
            const auto path=std::string("gfx/fonts/eu4-unicode/cache/")+(size==88?"zh-hans-map":"zh-hans-"+std::to_string(size));
            eu4unicode::ensure_player_font_atlas(root,path);
            const auto stem=root/std::filesystem::u8path(path);
            const auto fnt=std::filesystem::path(stem.wstring()+L".fnt");
            const auto dds=std::filesystem::path(stem.wstring()+L".dds");
            std::ifstream metrics(fnt);std::string line;int count=0;
            std::map<std::uint32_t,std::map<std::string,int>> records;
            while(std::getline(metrics,line)) {
                if(line.rfind("common ",0)==0) {
                    const auto p=properties(line);
                    require(p.at("lineHeight")==size&&p.at("scaleW")==2048&&p.at("scaleH")==4096,"Runtime atlas dimensions changed");
                }
                if(line.rfind("char ",0)==0) {
                    const auto p=properties(line);++count;
                    require(records.emplace(p.at("id"),p).second,"Duplicate runtime glyph");
                }
            }
            require(count==192&&records.count(0x2026),"Runtime basic font coverage is incomplete");
            std::ifstream texture(dds,std::ios::binary);std::array<std::uint32_t,32> header{};
            texture.read(reinterpret_cast<char*>(header.data()),sizeof(header));
            require(header[0]==0x20534444&&header[3]==4096&&header[4]==2048,"Runtime DDS header is invalid");
            require(std::filesystem::file_size(dds)==128+2048ull*4096*4,"Runtime DDS payload is truncated");
            for(const auto scalar:{0x41u,0xe9u,0x2026u}) {
                const auto& p=records.at(scalar);
                const auto reference=eu4unicode::rasterize_scalar(scalar,size);
                require(p.at("width")==reference.metrics.width&&p.at("height")==reference.metrics.height&&
                        p.at("xadvance")==reference.metrics.advance,"Runtime metrics do not match system glyphs");
                std::vector<std::uint8_t> alpha;
                for(int y=0;y<p.at("height");++y) {
                    texture.seekg(128+(static_cast<std::uint64_t>(p.at("y")+y)*2048+p.at("x"))*4);
                    for(int x=0;x<p.at("width");++x) {
                        std::uint32_t pixel=0;texture.read(reinterpret_cast<char*>(&pixel),4);
                        require((pixel&0xffffff)==(size==88?0u:0xffffffu),"Runtime font color differs from UI/map contract");
                        alpha.push_back(static_cast<std::uint8_t>(pixel>>24));
                    }
                }
                require(alpha==reference.alpha,"Runtime atlas contains pixels from a different font source");
            }
            const auto previous=std::filesystem::last_write_time(dds);
            eu4unicode::ensure_player_font_atlas(root,path);
            require(std::filesystem::last_write_time(dds)==previous,"Repeated font loads regenerated the atlas");
        }
        bool rejected=false;
        try { eu4unicode::ensure_player_font_atlas(root,"gfx/fonts/custom-mod-font"); }
        catch(const std::invalid_argument&) { rejected=true; }
        require(rejected,"Runtime generator accepted a mod-owned font path");
        std::cout<<"PASS: five system font atlases, exact base pixels, metrics, UI/map colors and process cache.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
