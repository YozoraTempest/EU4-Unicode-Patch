#include "native_font_atlas.hpp"
#include "scalar_glyph.hpp"
#include "native_font_draw.hpp"
#include "unicode_text.hpp"
#include "engine_string.hpp"
#include "formatted_paragraph.hpp"
#include "formatted_text.hpp"
#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <tuple>

namespace eu4unicode {
NativeTextureLookup original_texture_lookup=nullptr;
namespace {
using Microsoft::WRL::ComPtr;
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
    struct Paragraph {
        std::shared_ptr<const ShapedParagraph> layout;
        std::shared_ptr<const NativeParagraph> geometry;
        std::size_t bytes=0;
        std::uint64_t used=0;
    };
    std::map<std::tuple<bool,bool,float,float,std::string,std::vector<std::string>,std::vector<float>>,Paragraph> paragraphs;
    std::size_t paragraph_bytes=0;
    std::uint64_t paragraph_clock=0;
    std::uint32_t next_paragraph_token=0xf0000;
    std::map<std::uint32_t,std::uint32_t> free_paragraph_tokens;
};
struct GlyphPage { const void* anchor;std::uint32_t page; };
std::unordered_map<const NativeGlyph*,GlyphPage> glyph_pages;
constexpr std::uint64_t atlas_texture_budget=256ull*1024*1024;
std::filesystem::path fixture_path,font_path;
std::string atlas_prefix;
FontLog logger=nullptr;
std::shared_ptr<const TextFonts> text_fonts;
std::shared_ptr<const TextFonts> paragraph_fonts;
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
void load_font_files() {
    if(!font_files_checked) {
        font_files_checked=true;
        std::vector<std::filesystem::path> files;
        for(const auto name:{L"SourceHanSansSC-Regular.otf",L"PlangothicP1-Regular.ttf",L"PlangothicP2-Regular.ttf"}) {
            const auto file=font_path/name;
            if(!prefer_system||std::filesystem::is_regular_file(file)) files.push_back(file);
        }
        if(!files.empty()) {
            text_fonts=std::make_shared<TextFonts>(files);
            paragraph_fonts=std::make_shared<TextFonts>(files,prefer_system);
        }
        if(logger&&prefer_system) {
            char message[100];std::snprintf(message,sizeof(message),"Optional font files loaded: %zu",files.size());logger(message);
        }
    }
}
ScalarGlyph atlas_glyph(std::uint32_t scalar,int size) {
    if(prefer_system) {
        try { return rasterize_scalar(scalar,size); }
        catch(const std::domain_error&) {}
    }
    load_font_files();
    if(!text_fonts) throw std::domain_error("System fonts have no glyph; the optional font pack may supply it.");
    return rasterize_scalar(scalar,size,text_fonts);
}
Atlas::Glyph queue_glyph(Atlas& a,ScalarGlyph glyph) {
    if(glyph.metrics.width<=0||glyph.metrics.height<=0||glyph.metrics.width+2>a.width||
       glyph.metrics.height+2>a.height||glyph.alpha.size()!=static_cast<std::size_t>(glyph.metrics.width)*glyph.metrics.height)
        throw std::length_error("Native font glyph exceeds page dimensions");
    std::uint32_t page_index=0;int x=0,y=0,row=0;
    for(;page_index<a.pages.size();++page_index) {
        const auto& page=*a.pages[page_index];x=page.x;y=page.y;row=page.row;
        if(x+glyph.metrics.width+1>a.width) { x=1;y+=row+1;row=0; }
        if(y+glyph.metrics.height+1<=a.height) break;
    }
    if(page_index==a.pages.size()) {
        const auto page_bytes=static_cast<std::uint64_t>(a.width)*a.height*4;
        if((a.pages.size()+1)*page_bytes>atlas_texture_budget)
            throw std::length_error("Native font texture memory budget exhausted");
        a.pages.push_back(std::make_unique<Page>());x=1;y=1;row=0;
        if(logger) { char message[100];std::snprintf(message,sizeof(message),"Dynamic font page allocated: size=%d page=%u bytes=%llu",a.size,page_index,static_cast<unsigned long long>(page_bytes));logger(message); }
    }
    auto& page=*a.pages[page_index];
    glyph.metrics.x=static_cast<std::int16_t>(x);glyph.metrics.y=static_cast<std::int16_t>(y);
    page.pending.push_back({glyph.metrics,std::move(glyph.alpha)});
    page.x=x+glyph.metrics.width+1;page.y=y;page.row=(std::max)(row,static_cast<int>(glyph.metrics.height));
    return {glyph.metrics,page_index};
}
void release_paragraph_tokens(Atlas& atlas,std::uint32_t first,std::uint32_t count) {
    if(!count) return;
    auto after=atlas.free_paragraph_tokens.lower_bound(first);
    if(after!=atlas.free_paragraph_tokens.begin()) {
        const auto before=std::prev(after);
        if(before->first+before->second==first) {
            first=before->first;count+=before->second;atlas.free_paragraph_tokens.erase(before);
        }
    }
    if(after!=atlas.free_paragraph_tokens.end()&&first+count==after->first) {
        count+=after->second;atlas.free_paragraph_tokens.erase(after);
    }
    atlas.free_paragraph_tokens.emplace(first,count);
}
std::uint32_t allocate_paragraph_tokens(Atlas& atlas,std::uint32_t count) {
    for(auto range=atlas.free_paragraph_tokens.begin();range!=atlas.free_paragraph_tokens.end();++range) if(range->second>=count) {
        const auto first=range->first,remaining=range->second-count;atlas.free_paragraph_tokens.erase(range);
        if(remaining) atlas.free_paragraph_tokens.emplace(first+count,remaining);
        return first;
    }
    if(count>0xffffe-atlas.next_paragraph_token) throw std::length_error("Native paragraph transport token budget exhausted");
    const auto first=atlas.next_paragraph_token;atlas.next_paragraph_token+=count;return first;
}
void reserve_paragraph(Atlas& atlas,std::size_t bytes,const Atlas::Paragraph* keep=nullptr) {
    constexpr std::size_t budget=8ull*1024*1024;
    if(bytes>budget) throw std::length_error("Native paragraph exceeds cache budget");
    while(bytes>budget-atlas.paragraph_bytes) {
        auto oldest=atlas.paragraphs.end();
        for(auto entry=atlas.paragraphs.begin();entry!=atlas.paragraphs.end();++entry) {
            const auto& paragraph=entry->second;
            if(&paragraph==keep||(paragraph.geometry&&paragraph.geometry.use_count()!=1)||
               paragraph.layout.use_count()>(paragraph.geometry?2:1)) continue;
            if(oldest==atlas.paragraphs.end()||paragraph.used<oldest->second.used) oldest=entry;
        }
        if(oldest==atlas.paragraphs.end()) throw std::length_error("Active native paragraphs exceed cache budget");
        // Only CPU layout/record storage is evicted. The game's cached vertices
        // still refer to stable atlas pixels, so their slots and UVs stay intact.
        if(oldest->second.geometry) {
            for(const auto& record:oldest->second.geometry->records) glyph_pages.erase(&record);
            release_paragraph_tokens(atlas,oldest->second.geometry->first_token,
                static_cast<std::uint32_t>(oldest->second.geometry->records.size()));
        }
        atlas.paragraph_bytes-=oldest->second.bytes;atlas.paragraphs.erase(oldest);
    }
}
Atlas::Paragraph& paragraph_layout(Atlas& atlas,void* font,std::string_view text,float width,bool wrap,bool formatted) {
    const auto scale=*reinterpret_cast<const float*>(static_cast<const std::byte*>(font)+0x968);
    if(!std::isfinite(scale)||scale<=0) throw std::invalid_argument("Invalid native paragraph scale");
    auto content=std::make_shared<ParagraphText>(text,formatted,[font,scale](std::string_view name) {
        const auto table=*static_cast<void***>(font);
        if(!table||!table[0xe8/8]) throw std::domain_error("Native icon measurement is unavailable");
        const std::string terminated(name);
        const auto pixels=reinterpret_cast<int(*)(void*,const char*)>(table[0xe8/8])(font,terminated.c_str());
        return static_cast<float>(pixels)/scale;
    },[font](unsigned char code) {
        if(!native_paragraph_color) throw std::domain_error("Native color lookup is unavailable");
        std::uint32_t color=0;return native_paragraph_color(font,code,&color);
    });
    std::vector<float> icons;for(const auto& icon:content->icons()) icons.push_back(icon.advance);
    const auto key=std::make_tuple(wrap,formatted,width,scale,std::string(text),content->colors(),std::move(icons));
    const auto found=atlas.paragraphs.find(key);
    if(found!=atlas.paragraphs.end()) { found->second.used=++atlas.paragraph_clock;return found->second; }
    // Include DirectWrite text/cluster storage and both affinities of cached
    // visual caret stops, rather than charging only the lookup key.
    std::size_t color_bytes=0;for(const auto& color:content->colors()) color_bytes+=sizeof(std::string)+color.size();
    const auto estimate=sizeof(Atlas::Paragraph)+text.size()*96+color_bytes*2+4096;
    reserve_paragraph(atlas,estimate);
    std::shared_ptr<const ShapedParagraph> layout;
    if(prefer_system) layout=std::make_shared<ShapedParagraph>(text,atlas.size,width,wrap,nullptr,content);
    if(!layout||layout->missing_glyphs()) {
        load_font_files();
        if(paragraph_fonts) layout=std::make_shared<ShapedParagraph>(text,atlas.size,width,wrap,paragraph_fonts,content);
    }
    if(!layout||layout->missing_glyphs()) throw std::domain_error("Font collection has no glyph for shaped paragraph");
    auto& result=atlas.paragraphs.emplace(key,Atlas::Paragraph{std::move(layout),{},estimate,++atlas.paragraph_clock}).first->second;
    atlas.paragraph_bytes+=estimate;
    return result;
}
std::vector<ScalarGlyph> map_cluster_glyphs(const ShapedParagraph& paragraph,int width,int height) {
    if(paragraph.lines().size()!=1||!paragraph.objects().empty()) throw std::invalid_argument("Map labels require one plain line");
    struct Cluster { GlyphBitmap bitmap;float pen,x,y; };
    std::vector<Cluster> clusters;
    std::size_t bytes=0;
    for(const auto& run:paragraph.runs()) {
        std::vector<float> positions(run.advances.size()+1);
        for(std::size_t index=0;index<run.advances.size();++index) positions[index+1]=positions[index]+run.advances[index];
        const bool rtl=(run.bidi_level&1)!=0;
        for(const auto& cluster:run.clusters) {
            const auto first=cluster.first_glyph,last=first+cluster.glyph_count;
            auto slice=run;
            slice.clusters.clear();slice.text_start=cluster.text_start;slice.text_length=cluster.text_length;
            slice.glyphs.assign(run.glyphs.begin()+first,run.glyphs.begin()+last);
            slice.advances.assign(run.advances.begin()+first,run.advances.begin()+last);
            slice.offsets.assign(run.offsets.begin()+first,run.offsets.begin()+last);
            slice.baseline_x=run.baseline_x+(rtl?-positions[first]:positions[first]);
            auto bitmap=rasterize_glyph_run(slice);
            if(bitmap.width>static_cast<unsigned>(width-2)||bitmap.height>static_cast<unsigned>(height-2))
                throw std::length_error("Map cluster exceeds atlas page dimensions");
            bytes+=bitmap.alpha.size();if(bytes>16ull*1024*1024) throw std::length_error("Map label raster exceeds CPU budget");
            const auto x=std::floor(slice.baseline_x)+bitmap.left,y=std::floor(slice.baseline_y)+bitmap.top;
            const auto pen=run.baseline_x+(rtl?-positions[last]:positions[first]);
            if(!bitmap.width||!bitmap.height) { bitmap.width=bitmap.height=1;bitmap.alpha={0}; }
            clusters.push_back({std::move(bitmap),pen,x,y});
        }
    }
    std::stable_sort(clusters.begin(),clusters.end(),[](const auto& left,const auto& right){return left.pen<right.pen;});
    std::vector<ScalarGlyph> result;result.reserve(clusters.size());
    int pen=0;
    for(std::size_t index=0;index<clusters.size();++index) {
        auto& cluster=clusters[index];
        const auto next=index+1<clusters.size()?std::round(clusters[index+1].pen):std::ceil(paragraph.metrics().width);
        const auto advance=next-pen,x=std::round(cluster.x)-pen,y=std::round(cluster.y);
        if(advance<-32768||advance>32767||x<-32768||x>32767||y<-32768||y>32767)
            throw std::length_error("Map cluster geometry exceeds native capacity");
        ScalarGlyph glyph{};glyph.metrics.width=static_cast<std::int16_t>(cluster.bitmap.width);
        glyph.metrics.height=static_cast<std::int16_t>(cluster.bitmap.height);
        glyph.metrics.x_offset=static_cast<std::int16_t>(x);glyph.metrics.y_offset=static_cast<std::int16_t>(y);
        glyph.metrics.advance=static_cast<std::int16_t>(advance);glyph.alpha=std::move(cluster.bitmap.alpha);
        result.push_back(std::move(glyph));pen+=static_cast<int>(advance);
    }
    return result;
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
    prefer_system=prefer_system_fonts;font_files_checked=false;text_fonts.reset();paragraph_fonts.reset();
    configure_font_draw(log);
    configure_paragraph_log(log);
    if(logger&&prefer_system) logger("Font source: system fonts first; optional font files supplement missing glyphs.");
}
void register_font_atlas(void* object,std::string_view selected_path) noexcept {
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        const auto f=static_cast<std::byte*>(object);
        const auto& stored_path=*reinterpret_cast<const EngineString*>(f+0xe0);
        const auto path=selected_path.empty()?std::string_view(stored_path.data(),static_cast<std::size_t>(stored_path.size)):selected_path;
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
            // Measurement may run independently of rendering. Only CPU data
            // is produced here; the engine's texture lookup flushes it before
            // binding, on its existing graphics/resource execution path.
            found=a.glyphs.emplace(scalar,queue_glyph(a,atlas_glyph(scalar,a.size))).first;
            if(logger) { const auto& glyph=found->second.metrics;char message[128];std::snprintf(message,sizeof(message),"Dynamic glyph U+%X queued: size=%d rect=%d,%d,%d,%d advance=%d",scalar,a.size,glyph.x,glyph.y,glyph.width,glyph.height,glyph.advance);logger(message); }
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
std::shared_ptr<const ShapedParagraph> font_paragraph_layout(void* font,std::string_view text,float width,bool wrap,bool formatted) {
    if(!font||!needs_native_paragraph_shaping(text,formatted)) return {};
    std::lock_guard<std::recursive_mutex> lock(mutex);
    const auto table=reinterpret_cast<void* const*>(static_cast<const std::byte*>(font)+0x120);
    const auto found=bindings.find(identity(table));
    if(found==bindings.end()) return {};
    return paragraph_layout(*found->second,font,text,width,wrap,formatted).layout;
}
std::shared_ptr<const NativeParagraph> font_paragraph_geometry(void* font,std::string_view text,float width,bool wrap,bool formatted) {
    if(!font||!needs_native_paragraph_shaping(text,formatted)) return {};
    std::lock_guard<std::recursive_mutex> lock(mutex);
    const auto table=reinterpret_cast<void* const*>(static_cast<const std::byte*>(font)+0x120);
    const auto found=bindings.find(identity(table));
    if(found==bindings.end()) return {};
    auto& atlas=*found->second;
    auto& entry=paragraph_layout(atlas,font,text,width,wrap,formatted);
    if(entry.geometry) return entry.geometry;
    // Validate the complete transport before consuming any stable atlas slots.
    std::vector<ScalarGlyph> glyphs;
    std::string transport;
    std::string color;
    const auto& content=entry.layout->content();
    const auto scale=*reinterpret_cast<const float*>(static_cast<const std::byte*>(font)+0x968);
    auto select_color=[&](std::uint32_t id) {
        const auto& next=content.colors().at(id);
        transport+=paragraph_color_transition(color,next);color=next;
    };
    auto append_glyph=[&](ScalarGlyph glyph) {
        // Final invocation-local token values are assigned after cache eviction.
        transport+=encode(0xf0000+static_cast<std::uint32_t>(glyphs.size()));
        glyphs.push_back(std::move(glyph));
    };
    auto advance_glyph=[&](int advance) {
        ScalarGlyph blank{};blank.metrics.width=blank.metrics.height=1;blank.alpha={0};
        blank.metrics.advance=static_cast<std::int16_t>(advance);append_glyph(std::move(blank));
    };
    if(atlas.size==88) {
        for(auto& glyph:map_cluster_glyphs(*entry.layout,atlas.width,atlas.height)) append_glyph(std::move(glyph));
    } else {
    auto tiles=rasterize_paragraph(*entry.layout,static_cast<std::uint32_t>(atlas.width-2),static_cast<std::uint32_t>(atlas.height-2));
    for(std::size_t line=0;line<entry.layout->lines().size();++line) {
        const auto& metrics=entry.layout->lines()[line];
        const auto first=glyphs.size(),line_start=transport.size();
        for(auto& tile:tiles) if(tile.line==line) {
            if(tile.x<-32768||tile.x>32767||tile.y-metrics.top<-32768||tile.y-metrics.top>32767)
                throw std::length_error("Native shaped glyph offset exceeds capacity");
            ScalarGlyph glyph{};
            glyph.metrics.width=static_cast<std::int16_t>(tile.bitmap.width);
            glyph.metrics.height=static_cast<std::int16_t>(tile.bitmap.height);
            glyph.metrics.x_offset=static_cast<std::int16_t>(tile.x);
            glyph.metrics.y_offset=static_cast<std::int16_t>(tile.y-metrics.top);
            glyph.alpha=std::move(tile.bitmap.alpha);select_color(tile.style);append_glyph(std::move(glyph));
        }
        const auto plan=plan_paragraph_line(*entry.layout,line,scale);
        for(const auto& icon:plan.icons) {
            const auto placement=std::find_if(entry.layout->objects().begin(),entry.layout->objects().end(),
                [&](const auto& object){return object.id==icon.id;});
            select_color(placement->style);advance_glyph(icon.padding);
            transport+=content.icons().at(icon.id).command;
        }
        if(!plan.icons.empty()||glyphs.size()==first) { select_color(0);advance_glyph(plan.finish); }
        else glyphs.back().metrics.advance=static_cast<std::int16_t>(plan.finish);
        if(line+1==entry.layout->lines().size()) {
            transport+=paragraph_color_transition(color,"");color.clear();
        }
        // The native scratch word buffer is 256 bytes. A shaped run is a draw
        // unit, not an independently wrappable Unicode character.
        if(transport.size()-line_start>240||std::ceil(metrics.width)>32767)
            throw std::length_error("Native shaped line exceeds transport capacity");
        if(line+1<entry.layout->lines().size()) transport+='\n';
    }
    }
    if(transport.size()>32000||glyphs.size()>0xfffe)
        throw std::length_error("Native paragraph transport exceeds capacity");
    const auto estimate=sizeof(NativeParagraph)+transport.size()+glyphs.size()*(sizeof(NativeGlyph)+64);
    reserve_paragraph(atlas,estimate,&entry);
    auto geometry=std::make_shared<NativeParagraph>();
    geometry->layout=entry.layout;
    geometry->records.resize(glyphs.size());
    geometry->glyphs.reserve(glyphs.size());
    geometry->first_token=allocate_paragraph_tokens(atlas,static_cast<std::uint32_t>(glyphs.size()));
    try {
        for(std::size_t offset=0;offset<transport.size();) {
            const auto unit=native_text_unit(transport,offset,formatted);
            if(unit.kind==TextUnitKind::glyph&&unit.scalar>=0xf0000&&unit.scalar<0xf0000+glyphs.size()) {
                const auto assigned=encode(geometry->first_token+unit.scalar-0xf0000);
                std::copy(assigned.begin(),assigned.end(),transport.begin()+offset);
            }
            offset=unit.end;
        }
        geometry->draw_text=std::move(transport);
        for(std::size_t index=0;index<glyphs.size();++index) {
            const auto packed=queue_glyph(atlas,std::move(glyphs[index]));
            auto& record=geometry->records[index];record=packed.metrics;
            glyph_pages[&record]={&atlas,packed.page};geometry->glyphs.push_back(&record);
        }
    } catch(...) {
        for(const auto& record:geometry->records) glyph_pages.erase(&record);
        release_paragraph_tokens(atlas,geometry->first_token,static_cast<std::uint32_t>(glyphs.size()));
        throw;
    }
    atlas.paragraph_bytes+=estimate;entry.bytes+=estimate;entry.geometry=std::move(geometry);
    return entry.geometry;
}
void release_font_atlas(void* const* table) noexcept {
    if(!table) return;
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);const auto anchor=identity(table);
        const auto binding=bindings.find(anchor);
        const auto last=binding!=bindings.end()&&binding->second.use_count()==1?binding->second.get():nullptr;
        for(auto i=glyph_pages.begin();i!=glyph_pages.end();) {
            if(i->second.anchor==anchor||(last&&i->second.anchor==last)) i=glyph_pages.erase(i);else ++i;
        }
        bindings.erase(anchor);
        if(bindings.empty())
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
bool dynamic_font(void* object) noexcept {
    if(!object) return false;
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        const auto table=reinterpret_cast<void* const*>(static_cast<const std::byte*>(object)+0x120);
        return bindings.count(identity(table))!=0;
    } catch(...) { return false; }
}
std::uint32_t font_glyph_page(const NativeGlyph* glyph) noexcept {
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        const auto found=glyph_pages.find(glyph);return found==glyph_pages.end()?0:found->second.page;
    } catch(...) { return 0; }
}
FontTexturePages font_texture_pages(IDirect3DBaseTexture9* first) {
    if(!first) return {};
    std::lock_guard<std::recursive_mutex> lock(mutex);
    for(const auto& binding:bindings) {
        const auto& atlas=*binding.second;
        if(atlas.pages.front()->uploaded!=first) continue;
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
