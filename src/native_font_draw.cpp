#include "native_font_draw.hpp"
#include "native_paragraph.hpp"
#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace eu4unicode {
NativeMapGeometry original_map_geometry=nullptr;
NativeCountryGeometry original_country_geometry=nullptr;
NativeVertexUpload original_vertex_upload=nullptr;
NativeVertexCreate original_vertex_create=nullptr;
NativeVertexRelease original_vertex_release=nullptr;
namespace {
using Microsoft::WRL::ComPtr;
using IndexedDraw=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
using PrimitiveDraw=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT);
using CreateVertexBuffer=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9**,HANDLE*);
IndexedDraw original_indexed_draw=nullptr;
PrimitiveDraw original_primitive_draw=nullptr;
CreateVertexBuffer original_create_vertex_buffer=nullptr;
void* draw_entry=nullptr;
FontLog logger=nullptr;
thread_local bool capturing=false;
thread_local std::vector<bool> popup_capture_stack;
thread_local std::uint32_t current_page=0;
struct Upload { std::vector<std::byte> bytes;UINT stride=0; };
struct BufferUploads {
    std::map<UINT,Upload> ranges;
    IDirect3DDevice9* device=nullptr;
    D3DPOOL pool=D3DPOOL_DEFAULT;
    bool rejected=false;
};
// Borrow buffer identities. Holding a default-pool buffer would block Reset.
std::unordered_map<IDirect3DVertexBuffer9*,BufferUploads> uploads;
struct DrawBuffer { ComPtr<IDirect3DVertexBuffer9> buffer;UINT capacity=0; };
std::unordered_map<IDirect3DDevice9*,DrawBuffer> draw_buffers;
std::recursive_mutex mutex;
std::size_t cached_bytes=0;
constexpr std::size_t shadow_budget=64ull*1024*1024;
bool logged_draw=false;
bool logged_geometry=false,logged_upload=false,logged_contract=false;
void checked(HRESULT status) {
    if(FAILED(status)) throw std::runtime_error("Paged font drawing failed: "+std::to_string(static_cast<unsigned long>(status)));
}
void forget_buffer(IDirect3DVertexBuffer9* buffer) {
    const auto found=uploads.find(buffer);
    if(found==uploads.end()) return;
    for(const auto& upload:found->second.ranges) cached_bytes-=upload.second.bytes.size();
    uploads.erase(found);
}
bool page_tags(const std::byte* data,std::size_t bytes,UINT stride) {
    for(std::size_t offset=12;offset<bytes;offset+=stride) {
        float u=0;std::memcpy(&u,data+offset,sizeof(u));
        if(u>=2) return true;
    }
    return false;
}
void capture_vertices(void* wrapper,const void* data,int vertices,int offset,bool discard) noexcept {
    if(!wrapper||!data||!vertices||offset<0) return;
    IDirect3DVertexBuffer9* native=nullptr;
    bool tagged=false;
    try {
        const auto object=static_cast<const std::byte*>(wrapper);
        native=*reinterpret_cast<IDirect3DVertexBuffer9* const*>(object);
        const auto stride=*reinterpret_cast<const int*>(object+8);
        const auto capacity=*reinterpret_cast<const int*>(object+12);
        const auto count=vertices<0?capacity:(std::min)(vertices,capacity);
        if(!native||count<=0||(stride!=sizeof(MapFontVertex)&&stride!=sizeof(PopupFontVertex))) return;
        if(!capturing) {
            std::lock_guard<std::recursive_mutex> lock(mutex);
            if(!uploads.count(native)) return;
        }
        const auto bytes=static_cast<std::size_t>(count)*stride;
        const auto source=static_cast<const std::byte*>(data);
        tagged=page_tags(source,bytes,static_cast<UINT>(stride));
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if(discard) forget_buffer(native);
        auto found=uploads.find(native);
        const bool map_capture=capturing&&stride==sizeof(MapFontVertex);
        if(!tagged&&!map_capture&&found==uploads.end()) return;
        D3DVERTEXBUFFER_DESC description{};checked(native->GetDesc(&description));
        if(bytes>description.Size||static_cast<std::size_t>(offset)>description.Size-bytes)
            throw std::out_of_range("Font upload exceeds the native vertex buffer");
        ComPtr<IDirect3DDevice9> device;checked(native->GetDevice(&device));
        auto& cached=uploads[native];cached.device=device.Get();cached.pool=description.Pool;
        auto& ranges=cached.ranges;
        UINT begin=static_cast<UINT>(offset),end=begin+static_cast<UINT>(bytes);
        auto first=ranges.lower_bound(begin);
        if(first!=ranges.begin()) {
            const auto previous=std::prev(first);
            if(previous->first+previous->second.bytes.size()>=begin) first=previous;
        }
        auto last=first;std::size_t old_bytes=0;
        while(last!=ranges.end()&&last->first<=end) {
            if(last->second.stride!=static_cast<UINT>(stride)) throw std::runtime_error("Font vertex stride changed within a buffer");
            begin=(std::min)(begin,last->first);
            end=(std::max)(end,last->first+static_cast<UINT>(last->second.bytes.size()));
            old_bytes+=last->second.bytes.size();++last;
        }
        const auto merged_bytes=static_cast<std::size_t>(end-begin);
        if(cached_bytes-old_bytes+merged_bytes>shadow_budget) throw std::length_error("Font vertex CPU cache budget exceeded");
        Upload merged;merged.stride=static_cast<UINT>(stride);merged.bytes.resize(merged_bytes);
        for(auto current=first;current!=last;++current)
            std::memcpy(merged.bytes.data()+current->first-begin,current->second.bytes.data(),current->second.bytes.size());
        std::memcpy(merged.bytes.data()+static_cast<UINT>(offset)-begin,source,bytes);
        ranges.erase(first,last);cached_bytes-=old_bytes;
        if(map_capture||page_tags(merged.bytes.data(),merged.bytes.size(),merged.stride)) {
            ranges.emplace(begin,std::move(merged));cached_bytes+=merged_bytes;
        }
        cached.rejected=false;
        if(ranges.empty()) uploads.erase(native);
        if(logger&&!logged_upload) { logger("Font vertex upload captured.");logged_upload=true; }
    } catch(const std::exception& error) {
        if(logger) logger(error.what());
        if(native) {
            try {
                std::lock_guard<std::recursive_mutex> lock(mutex);
                if(tagged||uploads.count(native)) uploads[native].rejected=true;
            } catch(...) {}
        }
    }
}
const std::byte* font_vertices(const BufferUploads& cached,UINT offset,UINT bytes,UINT stride) {
    if(cached.rejected) throw std::runtime_error("Paged font vertex upload was rejected");
    auto range=cached.ranges.upper_bound(offset);
    if(range==cached.ranges.begin()) return nullptr;
    --range;
    const auto relative=static_cast<std::size_t>(offset-range->first);
    if(range->second.stride!=stride||relative%stride||relative>range->second.bytes.size()||bytes>range->second.bytes.size()-relative) return nullptr;
    return range->second.bytes.data()+relative;
}
HRESULT STDMETHODCALLTYPE create_vertex_buffer(IDirect3DDevice9* device,UINT length,DWORD usage,DWORD fvf,D3DPOOL pool,
                                               IDirect3DVertexBuffer9** result,HANDLE* shared) {
    const auto status=original_create_vertex_buffer(device,length,usage,fvf,pool,result,shared);
    if(SUCCEEDED(status)&&result&&*result) {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        // Reset or a new game can reuse the address of a destroyed buffer.
        forget_buffer(*result);
    }
    return status;
}
struct RestoreState {
    IDirect3DDevice9* device;
    ComPtr<IDirect3DVertexBuffer9> buffer;
    ComPtr<IDirect3DBaseTexture9> texture;
    UINT offset=0,stride=0;
    bool changed=false;
    ~RestoreState() {
        if(changed) {
            device->SetStreamSource(0,buffer.Get(),offset,stride);
            device->SetTexture(0,texture.Get());
        }
    }
};
HRESULT STDMETHODCALLTYPE draw_indexed_font(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,INT base,UINT minimum,
                                            UINT vertices,UINT start,UINT primitives) {
    RestoreState state{device};
    try {
        checked(device->GetTexture(0,&state.texture));
        const auto pages=font_texture_pages(state.texture.Get());
        if(pages.size()<=1) return original_indexed_draw(device,type,base,minimum,vertices,start,primitives);
        checked(device->GetStreamSource(0,&state.buffer,&state.offset,&state.stride));
        if(logger&&!logged_contract) {
            char message[160];std::snprintf(message,sizeof(message),"Map font draw contract: type=%u base=%d minimum=%u vertices=%u start=%u primitives=%u stride=%u",type,base,minimum,vertices,start,primitives,state.stride);
            logger(message);logged_contract=true;
        }
        // This is the observed 1.37.5 native map contract: independent quads,
        // the engine's shared quad index buffer, and a per-label base vertex.
        if(type!=D3DPT_TRIANGLELIST||minimum||start||base<0||vertices%4||primitives!=vertices/2||
           state.stride!=sizeof(MapFontVertex)||!state.buffer)
            return original_indexed_draw(device,type,base,minimum,vertices,start,primitives);
        std::lock_guard<std::recursive_mutex> lock(mutex);
        const auto buffer=uploads.find(state.buffer.Get());
        if(buffer==uploads.end()) return original_indexed_draw(device,type,base,minimum,vertices,start,primitives);
        const auto byte_offset=static_cast<std::uint64_t>(state.offset)+static_cast<std::uint64_t>(base)*state.stride;
        const auto byte_count=static_cast<std::uint64_t>(vertices)*sizeof(MapFontVertex);
        if(byte_offset>UINT_MAX||byte_count>UINT_MAX) throw std::out_of_range("Font vertex range exceeds the native buffer");
        const auto data=font_vertices(buffer->second,static_cast<UINT>(byte_offset),static_cast<UINT>(byte_count),sizeof(MapFontVertex));
        if(!data)
            throw std::runtime_error("Paged font geometry has no matching CPU upload");
        std::vector<MapFontVertex> physical(vertices);
        std::memcpy(physical.data(),data,vertices*sizeof(MapFontVertex));
        if(std::none_of(physical.begin(),physical.end(),[](const auto& vertex){return vertex.u>=2;}))
            return original_indexed_draw(device,type,base,minimum,vertices,start,primitives);
        const auto batches=split_font_quads(physical,pages.size());
        auto& scratch=draw_buffers[device];
        const auto bytes=vertices*static_cast<UINT>(sizeof(MapFontVertex));
        if(scratch.capacity<bytes) {
            scratch.buffer.Reset();scratch.capacity=0;
            const auto capacity=(std::max)(bytes,16384u);
            checked(device->CreateVertexBuffer(capacity,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&scratch.buffer,nullptr));
            scratch.capacity=capacity;
        }
        void* pixels=nullptr;
        checked(scratch.buffer->Lock(0,bytes,&pixels,D3DLOCK_DISCARD));
        std::memcpy(pixels,physical.data(),bytes);
        checked(scratch.buffer->Unlock());
        state.changed=true;
        checked(device->SetStreamSource(0,scratch.buffer.Get(),0,sizeof(MapFontVertex)));
        for(const auto& batch:batches) {
            checked(device->SetTexture(0,pages.at(batch.page).Get()));
            checked(original_indexed_draw(device,type,0,static_cast<UINT>(batch.first_quad*4),
                static_cast<UINT>(batch.quad_count*4),static_cast<UINT>(batch.first_quad*6),static_cast<UINT>(batch.quad_count*2)));
        }
        if(logger&&!logged_draw) {
            char message[128];std::snprintf(message,sizeof(message),"Paged map font draw: pages=%zu batches=%zu vertices=%u",pages.size(),batches.size(),vertices);
            logger(message);logged_draw=true;
        }
        return D3D_OK;
    } catch(const std::exception& error) {
        if(logger) logger(error.what());
        // Never submit tagged UVs after a failed upload or page selection.
        return D3DERR_INVALIDCALL;
    }
}
HRESULT STDMETHODCALLTYPE draw_popup_font(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,UINT start,UINT primitives) {
    RestoreState state{device};
    try {
        checked(device->GetTexture(0,&state.texture));
        const auto pages=font_texture_pages(state.texture.Get());
        if(pages.size()<=1) return original_primitive_draw(device,type,start,primitives);
        checked(device->GetStreamSource(0,&state.buffer,&state.offset,&state.stride));
        if(type!=D3DPT_TRIANGLELIST||primitives%2||state.stride!=sizeof(PopupFontVertex)||!state.buffer)
            return original_primitive_draw(device,type,start,primitives);
        const auto count=static_cast<std::uint64_t>(primitives)*3;
        const auto offset=static_cast<std::uint64_t>(state.offset)+static_cast<std::uint64_t>(start)*state.stride;
        if(count*sizeof(PopupFontVertex)>UINT_MAX||offset>UINT_MAX) throw std::out_of_range("Popup font vertex range is too large");
        std::lock_guard<std::recursive_mutex> lock(mutex);
        const auto buffer=uploads.find(state.buffer.Get());
        if(buffer==uploads.end()) return original_primitive_draw(device,type,start,primitives);
        const auto bytes=static_cast<UINT>(count*sizeof(PopupFontVertex));
        const auto data=font_vertices(buffer->second,static_cast<UINT>(offset),bytes,sizeof(PopupFontVertex));
        if(!data)
            return original_primitive_draw(device,type,start,primitives);
        std::vector<PopupFontVertex> physical(static_cast<std::size_t>(count));
        std::memcpy(physical.data(),data,bytes);
        if(std::none_of(physical.begin(),physical.end(),[](const auto& vertex){return vertex.u>=2;}))
            return original_primitive_draw(device,type,start,primitives);
        const auto batches=split_popup_glyphs(physical,pages.size());
        auto& scratch=draw_buffers[device];
        if(scratch.capacity<bytes) {
            scratch.buffer.Reset();scratch.capacity=0;
            const auto capacity=(std::max)(bytes,16384u);
            checked(device->CreateVertexBuffer(capacity,D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&scratch.buffer,nullptr));
            scratch.capacity=capacity;
        }
        void* target=nullptr;checked(scratch.buffer->Lock(0,bytes,&target,D3DLOCK_DISCARD));
        std::memcpy(target,physical.data(),bytes);checked(scratch.buffer->Unlock());
        state.changed=true;
        checked(device->SetStreamSource(0,scratch.buffer.Get(),0,sizeof(PopupFontVertex)));
        for(const auto& batch:batches) {
            checked(device->SetTexture(0,pages.at(batch.page).Get()));
            checked(original_primitive_draw(device,type,static_cast<UINT>(batch.first_quad*6),static_cast<UINT>(batch.quad_count*2)));
        }
        return D3D_OK;
    } catch(const std::exception& error) { if(logger) logger(error.what());return D3DERR_INVALIDCALL; }
}
}
void configure_font_draw(FontLog log) noexcept { logger=log; }
void install_font_draw_device(IDirect3DDevice9* device) {
    const auto table=*reinterpret_cast<void***>(device);
    if(draw_entry==table[82]) return;
    if(draw_entry) throw std::runtime_error("Multiple native font drawing implementations are unsupported");
    if(MH_CreateHook(table[82],reinterpret_cast<void*>(draw_indexed_font),reinterpret_cast<void**>(&original_indexed_draw))!=MH_OK||
       MH_CreateHook(table[81],reinterpret_cast<void*>(draw_popup_font),reinterpret_cast<void**>(&original_primitive_draw))!=MH_OK||
       MH_CreateHook(table[26],reinterpret_cast<void*>(create_vertex_buffer),reinterpret_cast<void**>(&original_create_vertex_buffer))!=MH_OK||
       MH_EnableHook(table[26])!=MH_OK||MH_EnableHook(table[82])!=MH_OK||MH_EnableHook(table[81])!=MH_OK)
        throw std::runtime_error("Paged font drawing hook failed");
    draw_entry=table[82];
}
void reset_font_draw_device(IDirect3DDevice9* device) noexcept {
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);draw_buffers.erase(device);
        for(auto current=uploads.begin();current!=uploads.end();) {
            if(current->second.device==device&&current->second.pool==D3DPOOL_DEFAULT) {
                const auto buffer=current->first;++current;forget_buffer(buffer);
            } else ++current;
        }
    } catch(...) {}
}
void release_font_draw_cache() noexcept {
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        uploads.clear();cached_bytes=0;draw_buffers.clear();logged_draw=false;
    } catch(...) {}
}
void build_map_font_geometry(void* owner,void* sector,void* labels,int count,void* font) {
    struct CaptureScope {
        bool previous;
        ~CaptureScope(){capturing=previous;}
    } scope{capturing};
    capturing=dynamic_map_font(font);
    if(logger&&capturing&&!logged_geometry) { logger("Map font geometry capture enabled.");logged_geometry=true; }
    begin_native_map_paragraph();
    struct ParagraphScope { ~ParagraphScope(){end_native_paragraph();} } paragraph_scope;
    original_map_geometry(owner,sector,labels,count,font);
}
bool build_country_font_geometry(void* owner,const int* provinces,int country,int label,int width,int height,void* name) {
    struct CaptureScope { bool previous;~CaptureScope(){capturing=previous;} } capture_scope{capturing};
    capturing=owner&&dynamic_map_font(*reinterpret_cast<void**>(static_cast<std::byte*>(owner)+0x30));
    begin_native_map_paragraph();
    struct ParagraphScope { ~ParagraphScope(){end_native_paragraph();} } paragraph_scope;
    return original_country_geometry(owner,provinces,country,label,width,height,name);
}
void upload_map_font_vertices(void* context,void* buffer,const void* data,int vertices,int offset,int mode) {
    // In 1.37.5 a negative vertex count uploads the wrapper's full capacity.
    // Static UI cache fills use that form; dynamic uploads use mode 0 to discard.
    const auto flags=buffer?*reinterpret_cast<const unsigned*>(static_cast<const std::byte*>(buffer)+16):0;
    capture_vertices(buffer,data,vertices,offset,(flags&0x200)&&mode!=1);
    original_vertex_upload(context,buffer,data,vertices,offset,mode);
}
void* create_font_vertices(void* context,const void* data,int vertices,int stride,bool dynamic,const void* name) {
    auto buffer=original_vertex_create(context,data,vertices,stride,dynamic,name);
    capture_vertices(buffer,data,vertices,0,true);
    return buffer;
}
void release_font_vertices(void* buffer) {
    if(buffer) {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        forget_buffer(*static_cast<IDirect3DVertexBuffer9**>(buffer));
    }
    original_vertex_release(buffer);
}
void mark_map_font_glyph(const NativeGlyph* glyph,MapFontVertex* vertices) noexcept {
    tag_font_vertices(vertices,6,font_glyph_page(glyph));
}
void mark_popup_font_glyph(const NativeGlyph* glyph,PopupFontVertex* vertices) noexcept { tag_font_vertices(vertices,6,font_glyph_page(glyph)); }
void begin_popup_font(void* font) { popup_capture_stack.push_back(capturing);capturing=dynamic_font(font); }
void end_popup_font() noexcept { if(!popup_capture_stack.empty()) { capturing=popup_capture_stack.back();popup_capture_stack.pop_back(); } }
void remember_map_font_glyph(const NativeGlyph* glyph) noexcept { current_page=font_glyph_page(glyph); }
void mark_current_map_font_glyph(MapFontVertex* vertices) noexcept { tag_font_vertices(vertices,6,current_page); }
}
