#include "native_font_atlas.hpp"
#include "scalar_glyph.hpp"
#include "native_font_draw.hpp"
#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace eu4unicode {
NativeTextureLookup original_texture_lookup=nullptr;
namespace {
using Microsoft::WRL::ComPtr;
struct EngineString {
    union { char inline_bytes[16]; const char* pointer; } storage;
    std::uint64_t size,capacity;
    std::string_view view() const { return {capacity<16?storage.inline_bytes:storage.pointer,static_cast<std::size_t>(size)}; }
};
struct Page {
    int x=1,y=1,row=0;
    ComPtr<IDirect3DTexture9> staging;
    // Borrowed comparison tokens: never retain a default-pool resource, which
    // would prevent the native device reset. The engine owns its GPU texture.
    IDirect3DTexture9* uploaded=nullptr;
    ComPtr<IDirect3DTexture9> texture;
    struct Pending { NativeGlyph metrics;std::vector<std::uint8_t> alpha; };
    std::vector<Pending> pending;
};
struct Atlas {
    void* manager=nullptr;
    int id=-1,size=0,width=0,height=0;
    std::filesystem::path dds;
    bool black=false;
    struct Glyph { NativeGlyph metrics;std::uint32_t page; };
    std::vector<std::unique_ptr<Page>> pages;
    std::unordered_map<std::uint32_t,Glyph> glyphs;
    std::unordered_set<std::uint32_t> rejected;
};
struct GlyphPage { const void* anchor;std::uint32_t page; };
std::unordered_map<const NativeGlyph*,GlyphPage> glyph_pages;
constexpr std::uint64_t map_texture_budget=256ull*1024*1024;
std::filesystem::path fixture_path,font_path;
std::string atlas_prefix;
FontLog logger=nullptr;
std::shared_ptr<const TextFonts> text_fonts;
bool prefer_system=false,font_files_checked=false;
std::unordered_map<const void*,std::shared_ptr<Atlas>> bindings;
std::recursive_mutex mutex;
using DeviceReset=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
DeviceReset original_reset=nullptr;
void* reset_entry=nullptr;
HRESULT STDMETHODCALLTYPE reset_font_device(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS* parameters) {
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        for(const auto& binding:bindings) for(auto& page:binding.second->pages) {
            page->uploaded=nullptr;page->texture.Reset();
        }
        reset_font_draw_device(device);
    }
    return original_reset(device,parameters);
}
void install_reset(IDirect3DDevice9* device) {
    auto entry=(*reinterpret_cast<void***>(device))[16];
    if(reset_entry==entry) return;
    if(reset_entry) throw std::runtime_error("Multiple native device reset implementations are unsupported");
    if(MH_CreateHook(entry,reinterpret_cast<void*>(reset_font_device),reinterpret_cast<void**>(&original_reset))!=MH_OK||
       MH_EnableHook(entry)!=MH_OK) throw std::runtime_error("Native device reset hook failed");
    reset_entry=entry;
}
void checked(HRESULT status) { if(FAILED(status)) throw std::runtime_error("Direct3D font texture operation failed: "+std::to_string(static_cast<unsigned long>(status))); }
const void* identity(void* const* table) { return table[0x41]?table[0x41]:table; }
int property(const std::string& line,const char* key) {
    const std::string name=std::string(key)+'=';
    auto pos=line.find(name);
    if(pos==std::string::npos) throw std::runtime_error("Missing generated font property");
    return std::stoi(line.substr(pos+name.size()));
}
IDirect3DTexture9* texture_of(void* wrapper) { return wrapper?*static_cast<IDirect3DTexture9**>(wrapper):nullptr; }
ScalarGlyph atlas_glyph(std::uint32_t scalar,int size) {
    if(prefer_system) {
        try { return rasterize_scalar(scalar,size); }
        catch(const std::domain_error&) {}
    }
    if(!font_files_checked) {
        font_files_checked=true;
        std::vector<std::filesystem::path> files;
        for(const auto name:{L"SourceHanSansSC-Regular.otf",L"PlangothicP1-Regular.ttf",L"PlangothicP2-Regular.ttf"}) {
            const auto file=font_path/name;
            if(!prefer_system||std::filesystem::is_regular_file(file)) files.push_back(file);
        }
        if(!files.empty()) text_fonts=std::make_shared<TextFonts>(files);
        if(logger&&prefer_system) {
            char message[100];std::snprintf(message,sizeof(message),"Optional font files loaded: %zu",files.size());logger(message);
        }
    }
    if(!text_fonts) throw std::domain_error("System fonts have no glyph; the optional font pack may supply it.");
    return rasterize_scalar(scalar,size,text_fonts);
}
void prepare_staging(Atlas& a,Page& page,IDirect3DTexture9* texture,bool initial) {
    ComPtr<IDirect3DDevice9> device;
    checked(texture->GetDevice(&device));
    install_reset(device.Get());
    install_font_draw_device(device.Get());
    if(page.staging) {
        ComPtr<IDirect3DDevice9> previous;
        checked(page.staging->GetDevice(&previous));
        if(device.Get()==previous.Get()) return;
        // Transfer existing pixels to the new device's system-memory resource.
        ComPtr<IDirect3DTexture9> replacement;
        checked(device->CreateTexture(a.width,a.height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&replacement,nullptr));
        D3DLOCKED_RECT old_lock{},new_lock{};
        checked(page.staging->LockRect(0,&old_lock,nullptr,D3DLOCK_READONLY));
        const auto status=replacement->LockRect(0,&new_lock,nullptr,0);
        if(FAILED(status)) { page.staging->UnlockRect(0); checked(status); }
        for(int row=0;row<a.height;++row)
            std::memcpy(static_cast<char*>(new_lock.pBits)+row*new_lock.Pitch,static_cast<char*>(old_lock.pBits)+row*old_lock.Pitch,a.width*4);
        page.staging->UnlockRect(0); checked(replacement->UnlockRect(0));
        page.staging=std::move(replacement);page.uploaded=nullptr;return;
    }
    std::ifstream source;
    std::array<std::uint32_t,32> header{};
    if(initial) {
        source.open(a.dds,std::ios::binary);
        source.read(reinterpret_cast<char*>(header.data()),sizeof(header));
        if(!source||header[0]!=0x20534444||header[1]!=124||header[3]!=static_cast<std::uint32_t>(a.height)||
       header[4]!=static_cast<std::uint32_t>(a.width)||header[19]!=32||header[20]!=0x41||header[22]!=32||
       header[23]!=0xff0000||header[24]!=0xff00||header[25]!=0xff||header[26]!=0xff000000)
            throw std::runtime_error("Generated font DDS format mismatch");
    }
    ComPtr<IDirect3DTexture9> staging;
    checked(device->CreateTexture(a.width,a.height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    D3DLOCKED_RECT pixels{};checked(staging->LockRect(0,&pixels,nullptr,0));
    for(int row=0;row<a.height;++row) {
        auto target=static_cast<std::uint32_t*>(static_cast<void*>(static_cast<char*>(pixels.pBits)+row*pixels.Pitch));
        if(initial) source.read(reinterpret_cast<char*>(target),a.width*4);
        else std::fill_n(target,a.width,a.black?0u:0xffffffu);
    }
    checked(staging->UnlockRect(0));
    if(initial&&(!source||source.peek()!=EOF)) throw std::runtime_error("Generated font DDS length mismatch");
    page.staging=std::move(staging);
}
void sync_page(Atlas& a,Page& page,IDirect3DTexture9* texture,bool initial) {
    if(!texture||(!page.staging&&page.pending.empty())||(texture==page.uploaded&&page.pending.empty())) return;
    D3DSURFACE_DESC desc{};checked(texture->GetLevelDesc(0,&desc));
    if(desc.Width!=static_cast<UINT>(a.width)||desc.Height!=static_cast<UINT>(a.height)||desc.Format!=D3DFMT_A8R8G8B8||desc.Pool!=D3DPOOL_DEFAULT)
        throw std::runtime_error("Native font GPU texture contract mismatch");
    prepare_staging(a,page,texture,initial);
    if(texture!=page.uploaded) checked(page.staging->AddDirtyRect(nullptr));
    for(const auto& glyph:page.pending) {
        const auto& g=glyph.metrics;
        RECT rect{g.x,g.y,g.x+g.width,g.y+g.height};
        D3DLOCKED_RECT pixels{};checked(page.staging->LockRect(0,&pixels,&rect,0));
        for(int y=0;y<g.height;++y) for(int x=0;x<g.width;++x) {
            const auto color=a.black?0u:0xffffffu;
            const auto alpha=glyph.alpha[static_cast<std::size_t>(y)*g.width+x];
            reinterpret_cast<std::uint32_t*>(static_cast<char*>(pixels.pBits)+y*pixels.Pitch)[x]=color|(static_cast<std::uint32_t>(alpha)<<24);
        }
        checked(page.staging->UnlockRect(0));
    }
    ComPtr<IDirect3DDevice9> device;checked(texture->GetDevice(&device));
    checked(device->UpdateTexture(page.staging.Get(),texture));page.uploaded=texture;
    if(logger&&!page.pending.empty()) { char message[100];std::snprintf(message,sizeof(message),"Dynamic font texture uploaded: size=%d glyphs=%zu",a.size,page.pending.size());logger(message); }
    page.pending.clear();
}
void sync(Atlas& a,void* wrapper) {
    const auto texture=texture_of(wrapper);
    if(!texture) return;
    ComPtr<IDirect3DDevice9> device;checked(texture->GetDevice(&device));
    for(std::size_t index=0;index<a.pages.size();++index) {
        auto& page=*a.pages[index];
        if(index&&!page.texture) {
            checked(device->CreateTexture(a.width,a.height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&page.texture,nullptr));
        }
        sync_page(a,page,index?page.texture.Get():texture,index==0);
        // ASCII-only initial pages have no pending upload. They still identify
        // the engine texture when another page is requested later.
        if(!index) page.uploaded=texture;
    }
}
}
void configure_font_atlases(const std::filesystem::path& fixture,const std::filesystem::path& fonts,FontLog log,std::string_view prefix,bool prefer_system_fonts) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    fixture_path=fixture;font_path=fonts;logger=log;atlas_prefix=prefix;
    prefer_system=prefer_system_fonts;font_files_checked=false;text_fonts.reset();
    configure_font_draw(log);
    if(logger&&prefer_system) logger("Font source: system fonts first; optional font files supplement missing glyphs.");
}
void register_font_atlas(void* object,std::string_view selected_path) noexcept {
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        const auto f=static_cast<std::byte*>(object);
        const auto path=selected_path.empty()?reinterpret_cast<const EngineString*>(f+0xe0)->view():selected_path;
        const std::array<std::pair<const char*,int>,5> names{{{"zh-hans-14",14},{"zh-hans-16",16},{"zh-hans-18",18},{"zh-hans-24",24},{"zh-hans-map",88}}};
        int size=0;for(const auto& name:names) if(path==atlas_prefix+name.first) size=name.second;
        if(!size) return;
        auto table=reinterpret_cast<void**>(f+0x120);
        const auto key=identity(table);
        if(!table[0x41]||bindings.count(key)) return;
        const auto width=*reinterpret_cast<int*>(f+0x978),height=*reinterpret_cast<int*>(f+0x97c);
        // Dynamic space is an explicit generated asset contract, never assumed in a
        // workshop atlas. Existing small atlases keep their static behavior.
        if(width!=2048||height!=4096) return;
        const auto context=*reinterpret_cast<std::byte**>(f+0x48);
        auto manager=*reinterpret_cast<void**>(context+0x480);
        const auto id=*reinterpret_cast<int*>(f+0x970);
        for(const auto& bound:bindings) if(bound.second->manager==manager&&bound.second->id==id) {
            bindings.emplace(key,bound.second);return;
        }
        auto atlas=std::make_shared<Atlas>();
        atlas->manager=manager;atlas->id=id;atlas->size=size;atlas->width=width;atlas->height=height;atlas->black=size==88;
        atlas->dds=fixture_path/(std::string(path)+".dds");
        std::ifstream metrics(fixture_path/(std::string(path)+".fnt"));
        std::string line;int occupied=1;bool common=false;
        while(std::getline(metrics,line)) {
            if(line.rfind("common ",0)==0) {
                if(property(line,"scaleW")!=width||property(line,"scaleH")!=height||property(line,"lineHeight")!=size)
                    throw std::runtime_error("Native font metrics contract mismatch");
                common=true;
            }
            if(line.rfind("char ",0)==0) occupied=(std::max)(occupied,property(line,"y")+property(line,"height")+1);
        }
        if(!common||occupied>=height) throw std::runtime_error("Font atlas has no dynamic region");
        auto page=std::make_unique<Page>();page->y=occupied;
        atlas->pages.push_back(std::move(page));
        bindings.emplace(key,std::move(atlas));
    } catch(const std::exception& error) { if(logger) logger(error.what()); }
}
NativeGlyph* find_dynamic_glyph(void* const* table,std::uint32_t scalar) noexcept {
    if(!table||scalar<=255||scalar>0x10ffff||(scalar>=0xd800&&scalar<=0xdfff)) return nullptr;
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        auto binding=bindings.find(identity(table));if(binding==bindings.end()) return nullptr;
        auto& a=*binding->second;
        if(a.rejected.count(scalar)) return nullptr;
        auto found=a.glyphs.find(scalar);
        if(found==a.glyphs.end()) {
            auto glyph=atlas_glyph(scalar,a.size);
            if(glyph.metrics.width+2>a.width||glyph.metrics.height+2>a.height) throw std::length_error("Native font glyph exceeds page dimensions");
            std::uint32_t page_index=0;int x=0,y=0,row=0;
            for(;page_index<a.pages.size();++page_index) {
                const auto& page=*a.pages[page_index];x=page.x;y=page.y;row=page.row;
                if(x+glyph.metrics.width+1>a.width) { x=1;y+=row+1;row=0; }
                if(y+glyph.metrics.height+1<=a.height) break;
            }
            if(page_index==a.pages.size()) {
                const auto page_bytes=static_cast<std::uint64_t>(a.width)*a.height*4;
                if(a.size!=88||(a.pages.size()+1)*page_bytes>map_texture_budget)
                    throw std::length_error("Native font texture memory budget exhausted");
                a.pages.push_back(std::make_unique<Page>());x=1;y=1;row=0;
                if(logger) { char message[100];std::snprintf(message,sizeof(message),"Dynamic font page allocated: size=%d page=%u bytes=%llu",a.size,page_index,static_cast<unsigned long long>(page_bytes));logger(message); }
            }
            auto& page=*a.pages[page_index];
            glyph.metrics.x=static_cast<std::int16_t>(x);glyph.metrics.y=static_cast<std::int16_t>(y);
            // Measurement may run independently of rendering. Only CPU data
            // is produced here; the engine's texture lookup flushes it before
            // binding, on its existing graphics/resource execution path.
            page.pending.push_back({glyph.metrics,std::move(glyph.alpha)});
            found=a.glyphs.emplace(scalar,Atlas::Glyph{glyph.metrics,page_index}).first;
            page.x=x+glyph.metrics.width+1;page.y=y;page.row=(std::max)(row,static_cast<int>(glyph.metrics.height));
            if(logger) { char message[128];std::snprintf(message,sizeof(message),"Dynamic glyph U+%X queued: size=%d rect=%d,%d,%d,%d advance=%d",scalar,a.size,x,y,glyph.metrics.width,glyph.metrics.height,glyph.metrics.advance);logger(message); }
        }
        auto record=allocate_unicode_glyph(table,scalar);
        if(!record) record=static_cast<NativeGlyph*>(find_unicode_glyph(table,scalar));
        if(record) { *record=found->second.metrics;glyph_pages[record]={identity(table),found->second.page}; }
        return record;
    } catch(const std::exception& error) {
        if(logger) logger(error.what());
        // Missing font coverage and exhausted memory budgets are stable failures. Device
        // loss/upload failure remains retryable after the engine recovers.
        if(dynamic_cast<const std::domain_error*>(&error)||dynamic_cast<const std::length_error*>(&error))
            try { std::lock_guard<std::recursive_mutex> lock(mutex);auto found=bindings.find(identity(table));if(found!=bindings.end()) found->second->rejected.insert(scalar); } catch(...) {}
        return nullptr;
    }
}
void release_font_atlas(void* const* table) noexcept {
    if(!table) return;
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);const auto anchor=identity(table);
        for(auto i=glyph_pages.begin();i!=glyph_pages.end();) {
            if(i->second.anchor==anchor) i=glyph_pages.erase(i);else ++i;
        }
        bindings.erase(anchor);
        if(std::none_of(bindings.begin(),bindings.end(),[](const auto& binding){return binding.second->size==88;}))
            release_font_draw_cache();
    } catch(...) {}
}
bool dynamic_map_font(void* object) noexcept {
    if(!object) return false;
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        const auto table=reinterpret_cast<void* const*>(static_cast<const std::byte*>(object)+0x120);
        const auto found=bindings.find(identity(table));
        return found!=bindings.end()&&found->second->size==88;
    } catch(...) { return false; }
}
std::uint32_t font_glyph_page(const NativeGlyph* glyph) noexcept {
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        const auto found=glyph_pages.find(glyph);return found==glyph_pages.end()?0:found->second.page;
    } catch(...) { return 0; }
}
FontTexturePages map_font_texture_pages(IDirect3DBaseTexture9* first) {
    if(!first) return {};
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for(const auto& binding:bindings) {
        const auto& atlas=*binding.second;
        if(atlas.size!=88||atlas.pages.front()->uploaded!=first) continue;
        FontTexturePages result;result.emplace_back(atlas.pages.front()->uploaded);
        for(std::size_t index=1;index<atlas.pages.size();++index) {
            const auto texture=atlas.pages[index]->texture.Get();
            if(!texture||atlas.pages[index]->uploaded!=texture||!atlas.pages[index]->pending.empty())
                throw std::runtime_error("Native font page is not uploaded");
            result.push_back(texture);
        }
        return result;
    }
    return {};
}
void* synchronize_font_texture(void* manager,int id) {
    auto wrapper=original_texture_lookup(manager,id);
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        for(const auto& binding:bindings) if(binding.second->manager==manager&&binding.second->id==id) { sync(*binding.second,wrapper);break; }
    } catch(const std::exception& error) { if(logger) logger(error.what()); }
    return wrapper;
}
}
