#include "linux/font_pages.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace eu4unicode::linux_native {
namespace {
template<class T> T read(std::uintptr_t address) {
    T value;std::memcpy(&value,reinterpret_cast<void*>(address),sizeof(value));return value;
}
SafetyHookInline *create_hook=nullptr,*update_hook=nullptr,*destroy_hook=nullptr;
SafetyHookInline *textures_hook=nullptr,*draw_hook=nullptr,*indexed_hook=nullptr;
SafetyHookInline *graphics_destroy_hook=nullptr;
struct Upload {std::vector<std::byte> bytes;};
struct Buffer {unsigned stride;std::map<unsigned,Upload> ranges;bool rejected=false;};
struct Texture {void* texture;void* sampler;};
struct Scratch {void* buffer=nullptr;unsigned capacity=0,stride=0;};
std::recursive_mutex mutex;
std::unordered_map<void*,Buffer> buffers;
std::unordered_map<void*,Texture> textures;
std::unordered_map<void*,Scratch> scratch_buffers;
std::size_t cached_bytes=0;
std::atomic<std::uint64_t> batches_drawn=0;
std::atomic<std::uint64_t> indexed_batches=0;
thread_local std::uint32_t map_page=0;
bool logged_error=false;
constexpr std::array callers{0x1b5e4dcu,0x1b6027du,0x205163cu,0x20533a6u,0x20539b3u,0x2053a3du,0x20552a6u};
void forget(void* buffer) {
    const auto found=buffers.find(buffer);if(found==buffers.end()) return;
    for(const auto& [offset,range]:found->second.ranges) cached_bytes-=range.bytes.size();
    buffers.erase(found);
}
bool tags(const std::byte* data,std::size_t size,unsigned stride) {
    for(std::size_t offset=12;offset<size;offset+=stride) if(read<float>(reinterpret_cast<std::uintptr_t>(data+offset))>=2) return true;
    return false;
}
void capture(void* buffer,const void* data,int vertices,unsigned offset,int mode,std::uintptr_t caller) {
    std::lock_guard lock(mutex);
    if(!buffer) return;
    const bool known=buffers.contains(buffer);
    if(!known&&std::find(callers.begin(),callers.end(),caller)==callers.end()) return;
    const auto stride=read<unsigned>(reinterpret_cast<std::uintptr_t>(buffer));
    const auto capacity=read<unsigned>(reinterpret_cast<std::uintptr_t>(buffer)+4);
    if(stride!=20&&stride!=28) return;
    if(mode==0) {forget(buffer);offset=0;}
    if(!data) return;
    const auto count=vertices>0?static_cast<unsigned>(vertices):capacity;
    const auto bytes=static_cast<std::size_t>(count)*stride;
    if(count>capacity||bytes>64ull*1024*1024||offset>static_cast<std::size_t>(capacity)*stride-bytes||offset%stride) {
        buffers.try_emplace(buffer,Buffer{stride,{},true}).first->second.rejected=true;
        throw std::out_of_range("Linux font vertex upload exceeds native buffer");
    }
    if(!bytes||(!known&&!tags(static_cast<const std::byte*>(data),bytes,stride))) return;
    auto& cached=buffers.try_emplace(buffer,Buffer{stride,{}}).first->second;
    try {
        if(count>capacity||offset>static_cast<std::size_t>(capacity)*stride-bytes||offset%stride)
            throw std::out_of_range("Linux font vertex upload exceeds native buffer");
        auto& ranges=cached.ranges;unsigned begin=offset,end=offset+static_cast<unsigned>(bytes);
        auto first=ranges.lower_bound(begin);
        if(first!=ranges.begin()) {const auto previous=std::prev(first);if(previous->first+previous->second.bytes.size()>=begin) first=previous;}
        auto last=first;std::size_t replaced=0;
        while(last!=ranges.end()&&last->first<=end) {
            begin=std::min(begin,last->first);end=std::max(end,last->first+static_cast<unsigned>(last->second.bytes.size()));
            replaced+=last->second.bytes.size();++last;
        }
        if(cached_bytes-replaced+end-begin>64ull*1024*1024) throw std::length_error("Linux font vertex shadow budget exceeded");
        Upload merged;merged.bytes.resize(end-begin);
        for(auto current=first;current!=last;++current)
            std::memcpy(merged.bytes.data()+current->first-begin,current->second.bytes.data(),current->second.bytes.size());
        std::memcpy(merged.bytes.data()+offset-begin,data,bytes);
        ranges.erase(first,last);cached_bytes-=replaced;ranges.emplace(begin,std::move(merged));cached_bytes+=end-begin;cached.rejected=false;
    } catch(...) {cached.rejected=true;throw;}
}
void* create(void* context,const void* data,int count,int stride,bool dynamic,const char* name) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    auto buffer=create_hook->call<void*>(context,data,count,stride,dynamic,name);
    try {capture(buffer,data,count,0,0,caller);} catch(const std::exception& error) {log("Linux font capture failed: %s",error.what());}
    return buffer;
}
void update(void* context,void* buffer,const void* data,int count,unsigned offset,int mode) {
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
    try {capture(buffer,data,count,offset,mode,caller);} catch(const std::exception& error) {log("Linux font capture failed: %s",error.what());}
    update_hook->call<void>(context,buffer,data,count,offset,mode);
}
void destroy(void* buffer) {
    {std::lock_guard lock(mutex);forget(buffer);}
    destroy_hook->call<void>(buffer);
}
void set_textures(void* context,int count,void* const* values,unsigned stages,unsigned slot,void* const* samplers) {
    textures_hook->call<void>(context,count,values,stages,slot,samplers);
    if(count>0&&slot==0&&(stages&1)) {
        std::lock_guard lock(mutex);
        textures[*static_cast<void**>(context)]={values?values[0]:nullptr,samplers?samplers[0]:nullptr};
    }
}
struct Restore {
    void* context;void* buffer;unsigned offset,stride;Texture texture;
    ~Restore() {
        reinterpret_cast<void(*)(void*,int,void* const*,unsigned,const unsigned*,const unsigned*)>(0x22ad49b)(context,1,&buffer,0,&offset,&stride);
        textures_hook->call<void>(context,1,&texture.texture,1u,0u,&texture.sampler);
    }
};
template<class Vertex> void submit(void* context,void* state,const std::byte* data,unsigned count,const std::vector<FontPage>& pages,bool indexed) {
    std::vector<Vertex> physical(count);std::memcpy(physical.data(),data,count*sizeof(Vertex));
    std::vector<FontPageSize> sizes;for(const auto& page:pages) sizes.push_back(page.size);
    const auto batches=[&] {
        if constexpr(sizeof(Vertex)==20) return indexed?split_font_quads(physical,sizes):split_font_triangles(physical,sizes);
        else return split_popup_glyphs(physical,sizes);
    }();
    auto& scratch=scratch_buffers[state];
    if(scratch.capacity<count||scratch.stride!=sizeof(Vertex)) {
        if(scratch.buffer) destroy_hook->call<void>(scratch.buffer);
        scratch={};
        scratch.buffer=create_hook->call<void*>(context,nullptr,std::max(count,2048u),static_cast<int>(sizeof(Vertex)),true,"EU4 Unicode font batches");
        if(!scratch.buffer) throw std::runtime_error("Linux font draw buffer allocation failed");
        scratch.capacity=std::max(count,2048u);scratch.stride=sizeof(Vertex);
    }
    update_hook->call<void>(context,scratch.buffer,physical.data(),count,0u,0);
    const unsigned offset=0,stride=sizeof(Vertex);
    reinterpret_cast<void(*)(void*,int,void* const*,unsigned,const unsigned*,const unsigned*)>(0x22ad49b)(context,1,&scratch.buffer,0,&offset,&stride);
    auto sampler=textures.at(state).sampler;
    for(const auto& batch:batches) {
        auto texture=pages.at(batch.page).texture;
        textures_hook->call<void>(context,1,&texture,1u,0u,&sampler);
        // Native map upload compacts CurveText's six temporary vertices into
        // four corners. Preserve the engine's bound index buffer and winding.
        if(indexed) {
            indexed_hook->call<void>(context,static_cast<int>(batch.quad_count*6),0,static_cast<int>(batch.first_quad*4));
            ++indexed_batches;
        } else draw_hook->call<void>(context,static_cast<int>(batch.quad_count*6),static_cast<int>(batch.first_quad*6));
        ++batches_drawn;
    }
}
bool draw_paged(void* context,int count,int first,bool indexed) {
    std::lock_guard lock(mutex);
    const auto state=*static_cast<void**>(context);
    const auto state_address=reinterpret_cast<std::uintptr_t>(state);
    const auto buffer=read<void*>(state_address+0x130);
    const auto found=buffers.find(buffer);if(found==buffers.end()) return false;
    if(found->second.rejected) return true;
    const auto texture=textures.find(state);if(texture==textures.end()) return false;
    if(first<0||read<unsigned>(state_address+8)!=4) return false;
    const auto stride=read<unsigned>(state_address+0x160),offset=read<unsigned>(state_address+0x150);
    if(count<=0) count=read<int>(state_address+(indexed?0x68:0x64));
    if(count<=0||count%6||stride!=found->second.stride) return false;
    if(indexed&&stride!=20) return false;
    const auto vertices=indexed?static_cast<unsigned>(count)/6*4:static_cast<unsigned>(count);
    const auto begin=static_cast<std::size_t>(offset)+static_cast<std::size_t>(first)*stride;
    if(begin>UINT_MAX) return true;
    auto range=found->second.ranges.upper_bound(static_cast<unsigned>(begin));
    if(range==found->second.ranges.begin()) return false;
    --range;
    const auto relative=begin-range->first,bytes=static_cast<std::size_t>(vertices)*stride;
    if(relative>range->second.bytes.size()||bytes>range->second.bytes.size()-relative) return false;
    const auto data=range->second.bytes.data()+relative;
    if(!tags(data,bytes,stride)) return false;
    Restore restore{context,buffer,offset,stride,texture->second};
    try {
        if(found->second.rejected) throw std::runtime_error("Rejected Linux font vertex upload");
        const auto pages=font_pages(texture->second.texture);
        if(pages.size()<2) throw std::runtime_error("Linux dynamic font pages are unavailable");
        if(stride==20) submit<MapFontVertex>(context,state,data,vertices,pages,indexed);
        else submit<PopupFontVertex>(context,state,data,vertices,pages,false);
    } catch(const std::exception& error) {
        if(!logged_error) {log("Linux paged font draw rejected: %s",error.what());logged_error=true;}
    }
    return true;
}
void draw(void* context,int count,int first) {if(!draw_paged(context,count,first,false)) draw_hook->call<void>(context,count,first);}
void indexed(void* context,int count,int unused,int first) {if(!draw_paged(context,count,first,true)) indexed_hook->call<void>(context,count,unused,first);}
void screen_mark(safetyhook::Context& c) {
    tag_font_vertices(reinterpret_cast<PopupFontVertex*>(0x3335110)+static_cast<unsigned>(c.r14),6,font_page(reinterpret_cast<NativeGlyph*>(c.r13)));
}
void texture_mark(safetyhook::Context& c) {
    tag_font_vertices(reinterpret_cast<PopupFontVertex*>(c.rax)+static_cast<unsigned>(c.r15)*6,6,font_page(reinterpret_cast<NativeGlyph*>(c.r11)));
}
void vertex_mark(safetyhook::Context& c) {
    tag_font_vertices(reinterpret_cast<MapFontVertex*>(c.rsi)+static_cast<unsigned>(c.r15),6,font_page(reinterpret_cast<NativeGlyph*>(c.rax)));
}
void map_remember(safetyhook::Context& c) {map_page=font_page(reinterpret_cast<NativeGlyph*>(c.r13));}
void map_mark(safetyhook::Context& c) {
    tag_font_vertices(reinterpret_cast<MapFontVertex*>(read<std::uintptr_t>(c.rbp-0x108))+static_cast<unsigned>(c.rbx),6,map_page);
}
void graphics_destroy(void* graphics) {
    {
        std::lock_guard lock(mutex);
        log("Linux paged font batches=%llu shadow-bytes=%zu",static_cast<unsigned long long>(batches_drawn.load()),cached_bytes);
        for(auto& [context,scratch]:scratch_buffers) if(scratch.buffer) destroy_hook->call<void>(scratch.buffer);
        scratch_buffers.clear();buffers.clear();textures.clear();cached_bytes=0;
        release_font_page_resources();
    }
    graphics_destroy_hook->call<void>(graphics);
}
}
void report_font_draw_counts() noexcept {
    std::lock_guard lock(mutex);
    log("Linux paged font batches=%llu indexed-batches=%llu shadow-bytes=%zu",
        static_cast<unsigned long long>(batches_drawn.load()),static_cast<unsigned long long>(indexed_batches.load()),cached_bytes);
}
void prepare_font_draw(Hooks& hooks) {
    verify_site(0x22ad49b);
    for(const auto caller:callers) verify_site(caller-5);
    create_hook=&hooks.add_inline(0x22ad267,reinterpret_cast<void*>(create));
    update_hook=&hooks.add_inline(0x22ad311,reinterpret_cast<void*>(update));
    destroy_hook=&hooks.add_inline(0x22ad2e9,reinterpret_cast<void*>(destroy));
    textures_hook=&hooks.add_inline(0x22ae01b,reinterpret_cast<void*>(set_textures));
    draw_hook=&hooks.add_inline(0x22ac51e,reinterpret_cast<void*>(draw));
    indexed_hook=&hooks.add_inline(0x22ac696,reinterpret_cast<void*>(indexed));
    graphics_destroy_hook=&hooks.add_inline(0x2078f7c,reinterpret_cast<void*>(graphics_destroy));
    hooks.add_mid(0x205335e,screen_mark);
    hooks.add_mid(0x205135b,texture_mark);
    hooks.add_mid(0x2055db6,vertex_mark);
    hooks.add_mid(0x1b5f46a,map_remember);
    hooks.add_mid(0x1b5fbad,map_mark);
}
}
