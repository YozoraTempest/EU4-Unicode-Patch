#include "font_atlas_assets.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

int wmain(int argc,wchar_t** argv) {
    try {
        if(argc<3) throw std::invalid_argument("Usage: fontpack UTF8_SOURCE OUTPUT_DIRECTORY [--font FILE ...] [--atlas-width N] [--atlas-height N] [--require-font-files 1]");
        std::vector<std::filesystem::path> files;
        int atlas_width=1024,reserve_height=0;
        bool require_files=false;
        for(int index=3;index<argc;index+=2) {
            if(index+1>=argc) throw std::invalid_argument("Missing fontpack option value");
            const std::wstring option(argv[index]);
            if(option==L"--font") files.emplace_back(argv[index+1]);
            else if(option==L"--atlas-width") atlas_width=std::stoi(argv[index+1]);
            else if(option==L"--atlas-height") reserve_height=std::stoi(argv[index+1]);
            else if(option==L"--require-font-files") require_files=std::wstring_view(argv[index+1])==L"1";
            else throw std::invalid_argument("Unknown fontpack option");
        }
        if(atlas_width<256||atlas_width>8192||(atlas_width&(atlas_width-1))||reserve_height<0||reserve_height>8192||
           (reserve_height&&(reserve_height&(reserve_height-1)))) throw std::invalid_argument("Atlas dimensions must be bounded powers of two");
        std::shared_ptr<const eu4unicode::TextFonts> fonts;
        if(!files.empty()) fonts=std::make_shared<eu4unicode::TextFonts>(files);
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
        for(const auto size:{14,16,18,24,88}) eu4unicode::write_font_atlas(scalars,argv[2],size,atlas_width,reserve_height,fonts,require_files);
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
