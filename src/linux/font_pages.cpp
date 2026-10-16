#include "linux/font_pages.hpp"
#include "scalar_glyph.hpp"
#include "formatted_paragraph.hpp"
#include "linux/paragraph.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
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
    struct Paragraph {
        std::shared_ptr<const ShapedParagraph> layout;
        std::shared_ptr<const FontParagraph> geometry;
        std::size_t bytes;
        std::uint64_t used;
    };
    std::map<std::tuple<bool,bool,float,float,std::string,std::vector<std::string>,std::vector<float>>,Paragraph> paragraphs;
    std::size_t paragraph_bytes=0;
    std::uint64_t paragraph_clock=0;
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
std::atomic<std::uint64_t> paragraph_layouts=0,paragraph_packed=0,paragraph_evicted=0;
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
        check_graphics_thread();
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
void reserve_paragraph(Atlas& atlas,std::size_t bytes,const Atlas::Paragraph* keep=nullptr) {
    constexpr std::size_t budget=8ull*1024*1024;
    if(bytes>budget) throw std::length_error("Linux paragraph exceeds CPU cache budget");
    while(bytes>budget-atlas.paragraph_bytes) {
        auto oldest=atlas.paragraphs.end();
        for(auto entry=atlas.paragraphs.begin();entry!=atlas.paragraphs.end();++entry) {
            const auto& item=entry->second;
            if(&item==keep||(item.geometry&&item.geometry.use_count()!=1)||
               item.layout.use_count()>(item.geometry?2:1)) continue;
            if(oldest==atlas.paragraphs.end()||item.used<oldest->second.used) oldest=entry;
        }
        if(oldest==atlas.paragraphs.end()) throw std::length_error("Active Linux paragraphs exceed CPU cache budget");
        if(oldest->second.geometry) for(const auto& glyph:oldest->second.geometry->glyphs) locations.erase(&glyph);
        // Cached native vertices own only UV/page values. Never reuse their
        // atlas pixels when evicting the CPU layout and glyph records.
        atlas.paragraph_bytes-=oldest->second.bytes;atlas.paragraphs.erase(oldest);++paragraph_evicted;
    }
}
Atlas::Paragraph& layout(Atlas& atlas,void* font,std::string_view text,float width,bool wrap,bool formatted) {
    const auto scale=read<float>(font,0x940);
    if(!std::isfinite(scale)||scale<=0) throw std::invalid_argument("Invalid Linux paragraph font scale");
    const auto vtable=read<void**>(font,0);
    auto content=std::make_shared<ParagraphText>(text,formatted,[font,scale,vtable](std::string_view name) {
        const std::string terminated(name);
        return reinterpret_cast<int(*)(void*,const char*)>(vtable[0x100/8])(font,terminated.c_str())/scale;
    },[font](unsigned char code) {
        std::uint32_t color=0;
        return reinterpret_cast<bool(*)(void*,char,std::uint32_t&)>(0x20519a2)(font,static_cast<char>(code),color);
    },[font,scale,vtable](std::string_view) {
        return reinterpret_cast<int(*)(void*)>(vtable[0x110/8])(font)/scale;
    },[font](std::uint32_t scalar) {
        const auto table=reinterpret_cast<void* const*>(static_cast<const char*>(font)+0x100);
        const auto glyph=scalar<=255?static_cast<const NativeGlyph*>(table[scalar]):
            static_cast<const NativeGlyph*>(find_unicode_glyph(table,scalar));
        return glyph?static_cast<float>(glyph->advance):-1.f;
    },[scale](std::string_view command) {
        // Verified native GetWidthOfString constants: numbered symbols eight
        // client pixels; currency sixteen. Native rendering keeps their commands.
        return (command.front()=='{'?8.f:16.f)/scale;
    });
    std::vector<float> icons;for(const auto& icon:content->icons()) icons.push_back(icon.advance);
    auto key=std::make_tuple(wrap,formatted,width,scale,std::string(text),content->colors(),std::move(icons));
    const auto found=atlas.paragraphs.find(key);
    if(found!=atlas.paragraphs.end()) {found->second.used=++atlas.paragraph_clock;return found->second;}
    std::size_t colors=0;for(const auto& color:content->colors()) colors+=sizeof(std::string)+color.size();
    const auto bytes=sizeof(Atlas::Paragraph)+text.size()*96+colors*2+4096;
    reserve_paragraph(atlas,bytes);
    std::call_once(fonts_once,load_optional_fonts);
    auto shaped=std::make_shared<ShapedParagraph>(text,atlas.size,width,wrap,optional_fonts,content);
    if(shaped->missing_glyphs()) throw std::domain_error("Linux font collection has no glyph for paragraph");
    auto& result=atlas.paragraphs.emplace(std::move(key),Atlas::Paragraph{std::move(shaped),{},bytes,++atlas.paragraph_clock}).first->second;
    atlas.paragraph_bytes+=bytes;++paragraph_layouts;return result;
}
void sync(Atlas& atlas) {
    // Establish the owner from a real font draw, never a resource lookup or
    // CPU-only measurement that may run while the engine prepares assets.
    check_graphics_thread();
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
            if(atlas->original_texture!=texture) {
                if(atlas->original_texture) texture_bindings.erase(atlas->original_texture);
                atlas->original_texture=texture;
            }
            texture_bindings[texture]=atlas;break;
        }
    } catch(const std::exception& error) {log("Linux font binding failed: %s",error.what());}
    return texture;
}
void destroy_texture(void* texture) {
    {
        std::lock_guard lock(mutex);
        const auto found=texture_bindings.find(texture);
        if(found!=texture_bindings.end()) {
            if(const auto atlas=found->second.lock()) {discard_gpu(*atlas);atlas->original_texture=nullptr;}
            texture_bindings.erase(found);
        }
    }
    destroy_texture_hook->call<void>(texture);
}
}
std::shared_ptr<const ShapedParagraph> font_paragraph_layout(void* font,std::string_view text,float width,bool wrap,bool formatted) {
    if(!font||!needs_native_paragraph_shaping(text,formatted)) return {};
    std::lock_guard lock(mutex);
    const auto table=reinterpret_cast<void* const*>(static_cast<const char*>(font)+0x100);
    const auto found=bindings.find(identity(table));
    if(found==bindings.end()||found->second->black) return {};
    return layout(*found->second,font,text,width,wrap,formatted).layout;
}
std::shared_ptr<const FontParagraph> font_paragraph_geometry(void* font,std::string_view text,float width,bool wrap,bool formatted) {
    if(!font||!needs_native_paragraph_shaping(text,formatted)) return {};
    std::lock_guard lock(mutex);
    const auto table=reinterpret_cast<void* const*>(static_cast<const char*>(font)+0x100);
    const auto found=bindings.find(identity(table));
    if(found==bindings.end()||found->second->black) return {};
    auto& atlas=*found->second;auto& entry=layout(atlas,font,text,width,wrap,formatted);
    if(entry.geometry) return entry.geometry;
    auto plan=plan_paragraph_transport(*entry.layout,atlas.supplemental_size,read<float>(font,0x940));
    const auto bytes=sizeof(FontParagraph)+plan.text.size()+plan.glyphs.size()*(sizeof(NativeGlyph)+64);
    reserve_paragraph(atlas,bytes,&entry);
    auto result=std::make_shared<FontParagraph>();result->layout=entry.layout;
    result->first_token=paragraph_token_start(plan,0xf0000);assign_paragraph_tokens(plan,result->first_token);
    result->text=std::move(plan.text);result->glyphs.resize(plan.glyphs.size());
    struct Position {int x,y,row;};
    std::vector<Position> before;before.reserve(atlas.pages.size());
    for(const auto& page:atlas.pages) before.push_back({page->x,page->y,page->row});
    const auto reserved=atlas.reserved_bytes;
    try {
        for(std::size_t index=0;index<plan.glyphs.size();++index) {
            const auto record=pack(atlas,std::move(plan.glyphs[index]));
            result->glyphs[index]=record.glyph;locations[&result->glyphs[index]]={&atlas,record.page};
        }
    } catch(...) {
        for(const auto& glyph:result->glyphs) locations.erase(&glyph);
        // No GPU upload or published vertex can observe these new slots while
        // the atlas lock is held. Pixels outside restored packing cursors are
        // unreachable and overwritten on the next successful allocation.
        atlas.pages.resize(before.size());reserved_total-=atlas.reserved_bytes-reserved;atlas.reserved_bytes=reserved;
        for(std::size_t index=0;index<before.size();++index) {
            auto& page=*atlas.pages[index];const auto& pos=before[index];page.x=pos.x;page.y=pos.y;page.row=pos.row;
        }
        throw;
    }
    atlas.paragraph_bytes+=bytes;entry.bytes+=bytes;entry.geometry=std::move(result);++paragraph_packed;
    return entry.geometry;
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
            discard_gpu(atlas);
            reserved_total-=atlas.reserved_bytes;
        }
        bindings.erase(found);
    } catch(const std::exception& error) {log("Linux font release failed: %s",error.what());}
}
NativeGlyph* find_font_glyph(void* const* table,std::uint32_t scalar,const char* origin) noexcept {
    if(!table) return nullptr;
    if(auto glyph=find_paragraph_glyph(table,scalar)) return glyph;
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
                ++queued;log("Linux dynamic glyph U+%X size=%d page=%u origin=%s",scalar,atlas.size,record->second.page,origin);
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
    sync(*atlas);
    std::vector<FontPage> result{{original,atlas->original_size}};
    for(const auto& page:atlas->pages) result.push_back({page->texture,page->size});
    return result;
}
void prepare_font_pages(Hooks& hooks) {
    verify_site(0x229f203);verify_site(0x22adbb4);verify_site(0x20519a2);
    lookup_hook=&hooks.add_inline(0x22a8a2e,reinterpret_cast<void*>(lookup));
    destroy_texture_hook=&hooks.add_inline(0x22adc83,reinterpret_cast<void*>(destroy_texture));
}
void report_font_page_counts() noexcept {
    log("Linux font pages queued-glyphs=%llu uploads=%llu",static_cast<unsigned long long>(queued.load()),static_cast<unsigned long long>(uploaded.load()));
    log("Linux paragraphs layouts=%llu packed=%llu evicted=%llu",static_cast<unsigned long long>(paragraph_layouts.load()),
        static_cast<unsigned long long>(paragraph_packed.load()),static_cast<unsigned long long>(paragraph_evicted.load()));
}
void release_font_page_resources() {
    std::lock_guard lock(mutex);
    for(auto& [key,atlas]:bindings) discard_gpu(*atlas);
    locations.clear();texture_bindings.clear();bindings.clear();reserved_total=0;graphics_thread={};
}
}
