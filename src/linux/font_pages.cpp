#include "linux/font_pages.hpp"
#include "scalar_glyph.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace eu4unicode::linux_native {
namespace {
template<class T> T read(const void* base,std::size_t offset) {
    T value;std::memcpy(&value,static_cast<const char*>(base)+offset,sizeof(value));return value;
}
struct Page {
    FontPageSize size;
    int x=1,y=1,row=0;
    std::vector<std::uint32_t> pixels;
    void* texture=nullptr;
    bool dirty=true;
};
struct Record {NativeGlyph glyph;std::uint32_t page;};
struct Atlas {
    void* manager;
    int id,size;
    bool black;
    FontPageSize original_size,supplemental_size;
    void* original_texture=nullptr;
    std::vector<std::unique_ptr<Page>> pages;
    std::unordered_map<std::uint32_t,Record> glyphs;
    std::unordered_set<std::uint32_t> rejected;
    std::uint64_t reserved_bytes=0;
};
struct Location {Atlas* atlas;std::uint32_t page;};
std::recursive_mutex mutex;
std::unordered_map<const void*,std::shared_ptr<Atlas>> bindings;
std::unordered_map<const NativeGlyph*,Location> locations;
std::unordered_map<void*,std::weak_ptr<Atlas>> texture_bindings;
SafetyHookInline *lookup_hook=nullptr,*destroy_texture_hook=nullptr;
std::thread::id graphics_thread;
std::shared_ptr<const TextFonts> optional_fonts;
std::once_flag fonts_once;
std::atomic<std::uint64_t> queued=0,uploaded=0;
std::uint64_t reserved_total=0;
const void* identity(void* const* table) {return table[0x41]?table[0x41]:table;}
void check_graphics_thread() {
    const auto current=std::this_thread::get_id();
    if(graphics_thread==std::thread::id{}) graphics_thread=current;
    if(graphics_thread!=current) throw std::runtime_error("Font GPU operation outside native graphics thread");
}
void load_optional_fonts() {
    Dl_info info{};
    if(!dladdr(reinterpret_cast<void*>(load_optional_fonts),&info)||!info.dli_fname)
        throw std::runtime_error("Cannot locate optional Linux font directory");
    const auto directory=std::filesystem::path(info.dli_fname).parent_path()/"fonts";
    std::vector<std::filesystem::path> files;
    for(const auto name:{"SourceHanSansSC-Regular.otf","PlangothicP1-Regular.ttf","PlangothicP2-Regular.ttf"})
        if(std::filesystem::is_regular_file(directory/name)) files.push_back(directory/name);
    if(!files.empty()) optional_fonts=std::make_shared<TextFonts>(files,true);
    log("Linux dynamic font source=system optional-files=%zu",files.size());
}
ScalarGlyph raster(std::uint32_t scalar,int size) {
    std::call_once(fonts_once,load_optional_fonts);
    return rasterize_scalar(scalar,size,optional_fonts);
}
void discard_gpu(Atlas& atlas) {
    for(auto& page:atlas.pages) if(page->texture) {
        destroy_texture_hook->call<void>(page->texture);page->texture=nullptr;page->dirty=true;
    }
}
Record pack(Atlas& atlas,ScalarGlyph glyph) {
    const auto width=glyph.metrics.width,height=glyph.metrics.height;
    if(width<=0||height<=0||width+2>static_cast<int>(atlas.supplemental_size.width)||
       height+2>static_cast<int>(atlas.supplemental_size.height)||glyph.alpha.size()!=static_cast<std::size_t>(width)*height)
        throw std::length_error("Dynamic glyph exceeds Linux font page");
    std::size_t index=0;int x=0,y=0,row=0;
    for(;index<atlas.pages.size();++index) {
        const auto& page=*atlas.pages[index];x=page.x;y=page.y;row=page.row;
        if(x+width+1>static_cast<int>(page.size.width)) {x=1;y+=row+1;row=0;}
        if(y+height+1<=static_cast<int>(page.size.height)) break;
    }
    if(index==atlas.pages.size()) {
        const auto bytes=atlas.supplemental_size.rgba_bytes();
        // Account for both retained CPU pixels and the eventual GPU page.
        check_font_page_budget(reserved_total*2+bytes,0,atlas.supplemental_size);
        auto page=std::make_unique<Page>();page->size=atlas.supplemental_size;
        page->pixels.assign(static_cast<std::size_t>(page->size.width)*page->size.height,atlas.black?0:0xffffff);
        atlas.pages.push_back(std::move(page));atlas.reserved_bytes+=bytes;reserved_total+=bytes;x=1;y=1;row=0;
        log("Linux font page reserved size=%d page=%zu width=%u height=%u cpu-gpu-bytes=%llu",
            atlas.size,index+1,atlas.supplemental_size.width,atlas.supplemental_size.height,
            static_cast<unsigned long long>(atlas.reserved_bytes*2));
    }
    auto& page=*atlas.pages[index];
    for(int py=0;py<height;++py) for(int px=0;px<width;++px)
        page.pixels[static_cast<std::size_t>(y+py)*page.size.width+x+px]=
            (atlas.black?0:0xffffff)|(static_cast<std::uint32_t>(glyph.alpha[static_cast<std::size_t>(py)*width+px])<<24);
    page.x=x+width+1;page.y=y;page.row=std::max(row,static_cast<int>(height));page.dirty=true;
    glyph.metrics.x=static_cast<std::int16_t>(x);glyph.metrics.y=static_cast<std::int16_t>(y);
    return {glyph.metrics,static_cast<std::uint32_t>(index+1)};
}
void sync(Atlas& atlas,void* original) {
    check_graphics_thread();
    if(atlas.original_texture!=original) {
        if(atlas.original_texture) texture_bindings.erase(atlas.original_texture);
        atlas.original_texture=original;
    }
    for(auto& page:atlas.pages) {
        if(!page->texture) {
            // Verified format 11: GL_RGBA8, BGRA, GL_UNSIGNED_BYTE; type 0: 2D.
            page->texture=reinterpret_cast<void*(*)(void*,const void*,unsigned,unsigned,int,int,bool,const char*)>(0x229f203)(
                nullptr,page->pixels.data(),page->size.width,page->size.height,11,0,false,"EU4 Unicode font page");
            if(!page->texture||!read<unsigned>(page->texture,4)) throw std::runtime_error("Native Linux font texture creation failed");
        } else if(page->dirty) {
            reinterpret_cast<void(*)(void*,const void*,int,int,int,int,int,void*)>(0x22adbb4)(
                nullptr,page->pixels.data(),0,page->size.width,page->size.height,0,4,page->texture);
        } else continue;
        page->dirty=false;++uploaded;
    }
}
void* lookup(void* manager,int id) {
    auto texture=lookup_hook->call<void*>(manager,id);
    if(!texture) return texture;
    try {
        std::lock_guard lock(mutex);
        for(const auto& [key,atlas]:bindings) if(atlas->manager==manager&&atlas->id==id) {
            sync(*atlas,texture);texture_bindings[texture]=atlas;break;
        }
    } catch(const std::exception& error) {log("Linux font upload failed: %s",error.what());}
    return texture;
}
void destroy_texture(void* texture) {
    {
        std::lock_guard lock(mutex);
        const auto found=texture_bindings.find(texture);
        if(found!=texture_bindings.end()) {
            if(const auto atlas=found->second.lock()) {check_graphics_thread();discard_gpu(*atlas);atlas->original_texture=nullptr;}
            texture_bindings.erase(found);
        }
    }
    destroy_texture_hook->call<void>(texture);
}
}
void register_font_pages(void* font) {
    std::lock_guard lock(mutex);
    const auto table=reinterpret_cast<void* const*>(static_cast<const char*>(font)+0x100);
    if(!table[0x41]||bindings.contains(identity(table))) return;
    const auto graphics=read<void*>(font,0x40);
    const auto size=read<int>(font,0x938);
    const auto width=read<unsigned>(font,0x950),height=read<unsigned>(font,0x954);
    if(!graphics||size<=0||size>512||width<=2||height<=2||width>16384||height>16384) return;
    const auto manager=read<void*>(graphics,0x458);const auto id=read<int>(font,0x948);
    auto name=std::filesystem::path(reinterpret_cast<const String*>(static_cast<const char*>(font)+0xc0)->view()).filename().string();
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char value){return std::tolower(value);});
    const auto black=name.starts_with("map")||name=="tahoma_60"||name.ends_with("-map")||name.ends_with("_map");
    for(const auto& [key,atlas]:bindings) if(atlas->manager==manager&&atlas->id==id&&atlas->size==size&&atlas->black==black) {
        bindings.emplace(identity(table),atlas);return;
    }
    auto atlas=std::make_shared<Atlas>();atlas->manager=manager;atlas->id=id;atlas->size=size;atlas->black=black;
    atlas->original_size={width,height};atlas->supplemental_size=supplemental_font_page_size(atlas->original_size);
    bindings.emplace(identity(table),std::move(atlas));
}
void release_font_pages(void* const* table) noexcept {
    try {
        std::lock_guard lock(mutex);const auto found=bindings.find(identity(table));
        if(found==bindings.end()) return;
        if(found->second.use_count()==1) {
            auto& atlas=*found->second;
            for(auto i=locations.begin();i!=locations.end();) if(i->second.atlas==&atlas) i=locations.erase(i);else ++i;
            if(atlas.original_texture) texture_bindings.erase(atlas.original_texture);
            if(!atlas.pages.empty()) {check_graphics_thread();discard_gpu(atlas);}
            reserved_total-=atlas.reserved_bytes;
        }
        bindings.erase(found);
    } catch(const std::exception& error) {log("Linux font release failed: %s",error.what());}
}
NativeGlyph* find_font_glyph(void* const* table,std::uint32_t scalar) noexcept {
    if(!table) return nullptr;
    if(scalar<=255&&table[scalar]) return static_cast<NativeGlyph*>(table[scalar]);
    if(auto existing=static_cast<NativeGlyph*>(find_unicode_glyph(table,scalar))) return existing;
    if(scalar<0x20||scalar>0x10ffff||(scalar>=0xd800&&scalar<=0xdfff)) return nullptr;
    try {
        std::lock_guard lock(mutex);const auto found=bindings.find(identity(table));
        if(found==bindings.end()) return nullptr;
        auto& atlas=*found->second;if(atlas.rejected.contains(scalar)) return nullptr;
        try {
            auto record=atlas.glyphs.find(scalar);
            if(record==atlas.glyphs.end()) {
                record=atlas.glyphs.emplace(scalar,pack(atlas,raster(scalar,atlas.size))).first;
                ++queued;log("Linux dynamic glyph U+%X size=%d page=%u",scalar,atlas.size,record->second.page);
            }
            auto result=&record->second.glyph;locations[result]={&atlas,record->second.page};return result;
        } catch(const std::domain_error& error) {atlas.rejected.insert(scalar);log("Linux font missing U+%X: %s",scalar,error.what());}
          catch(const std::length_error& error) {atlas.rejected.insert(scalar);log("Linux font budget U+%X: %s",scalar,error.what());}
    } catch(const std::exception& error) {log("Linux dynamic glyph failed U+%X: %s",scalar,error.what());}
    return nullptr;
}
std::uint32_t font_page(const NativeGlyph* glyph) noexcept {
    std::lock_guard lock(mutex);const auto found=locations.find(glyph);return found==locations.end()?0:found->second.page;
}
std::vector<FontPage> font_pages(void* original) {
    std::lock_guard lock(mutex);const auto found=texture_bindings.find(original);
    if(found==texture_bindings.end()) return {};
    const auto atlas=found->second.lock();if(!atlas) return {};
    sync(*atlas,original);
    std::vector<FontPage> result{{original,atlas->original_size}};
    for(const auto& page:atlas->pages) result.push_back({page->texture,page->size});
    return result;
}
void prepare_font_pages(Hooks& hooks) {
    verify_site(0x229f203);verify_site(0x22adbb4);
    lookup_hook=&hooks.add_inline(0x22a8a2e,reinterpret_cast<void*>(lookup));
    destroy_texture_hook=&hooks.add_inline(0x22adc83,reinterpret_cast<void*>(destroy_texture));
}
void report_font_page_counts() noexcept {
    log("Linux font pages queued-glyphs=%llu uploads=%llu",static_cast<unsigned long long>(queued.load()),static_cast<unsigned long long>(uploaded.load()));
}
void release_font_page_resources() {
    std::lock_guard lock(mutex);
    for(auto& [key,atlas]:bindings) discard_gpu(*atlas);
    locations.clear();texture_bindings.clear();bindings.clear();reserved_total=0;graphics_thread={};
}
}
