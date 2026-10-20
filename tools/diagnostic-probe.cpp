#include "unicode_layout.hpp"
#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include "icu_runtime.hpp"
#include <dlfcn.h>
#endif

namespace {
std::string json(const std::string& value) {
    std::string result="\"";
    const char* hex="0123456789abcdef";
    for(const unsigned char ch:value) {
        if(ch=='"'||ch=='\\') {result+='\\';result+=static_cast<char>(ch);}
        else if(ch<32) {result+="\\u00";result+=hex[ch>>4];result+=hex[ch&15];}
        else result+=static_cast<char>(ch);
    }
    return result+'"';
}
}
int main() {
    using namespace eu4unicode;
    try {
#ifndef _WIN32
        icu_runtime::ensure_loaded();
#endif
        const auto boundaries=grapheme_boundaries(u8"中e\u0301𠮷");
        std::cout<<"{\"schema_version\":1,\"unicode_services\":"<<(boundaries.size()==4?"true":"false")
                 <<",\"font_scope\":\"system fonts in the diagnostic process\",\"fonts\":[";
        bool first=true;
        for(const auto codepoint:{0x4e2du,0x20bb7u,0x0627u,0x05d0u,0x0915u,0x0e01u}) {
            if(!first) std::cout<<',';
            first=false;
            const auto text=encode(codepoint);
            TextLayout layout(text,24,128,128);
            const auto runs=layout.glyph_runs();
            const bool covered=!runs.empty()&&std::all_of(runs.begin(),runs.end(),[](const auto& run) {
                return !run.glyphs.empty()&&std::none_of(run.glyphs.begin(),run.glyphs.end(),[](auto glyph){return glyph==0;});
            });
            std::cout<<"{\"codepoint\":"<<codepoint<<",\"text\":"<<json(text)<<",\"covered\":"<<(covered?"true":"false")<<",\"families\":[";
            std::set<std::string> families;
            for(const auto& run:runs) families.insert(run.font_family);
            bool separator=false;
            for(const auto& family:families) {if(separator) std::cout<<',';separator=true;std::cout<<json(family);}
            std::cout<<"]}";
        }
        std::cout<<"],\"libraries\":[";
        std::set<std::string> paths;
#ifdef _WIN32
        for(const auto* name:{L"icu.dll",L"dwrite.dll",L"d2d1.dll",L"windowscodecs.dll"}) {
            const auto module=GetModuleHandleW(name);
            if(!module) continue;
            std::wstring path(32768,L'\0');
            const auto length=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
            if(!length||length>=path.size()) continue;
            path.resize(length);
            const auto bytes=WideCharToMultiByte(CP_UTF8,0,path.data(),static_cast<int>(path.size()),nullptr,0,nullptr,nullptr);
            std::string utf8(static_cast<std::size_t>(bytes),'\0');
            WideCharToMultiByte(CP_UTF8,0,path.data(),static_cast<int>(path.size()),utf8.data(),bytes,nullptr,nullptr);
            paths.insert(std::move(utf8));
        }
#else
        std::ifstream maps("/proc/self/maps");std::string line;
        while(std::getline(maps,line)) {
            const auto begin=line.find('/');
            if(begin==std::string::npos) continue;
            auto path=line.substr(begin);
            if(path.find("libicu")!=std::string::npos||path.find("libpango")!=std::string::npos||
               path.find("libcairo")!=std::string::npos||path.find("libfontconfig")!=std::string::npos) paths.insert(path);
        }
#endif
        bool separator=false;
        for(const auto& path:paths) {if(separator) std::cout<<',';separator=true;std::cout<<json(path);}
        std::cout<<"]}\n";
        return boundaries.size()==4?0:1;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
