#include "native_font_draw.hpp"
#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace eu4unicode {
NativeMapGeometry original_map_geometry=nullptr;
NativeVertexUpload original_vertex_upload=nullptr;
namespace {
using Microsoft::WRL::ComPtr;
using IndexedDraw=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
using CreateVertexBuffer=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9**,HANDLE*);
IndexedDraw original_indexed_draw=nullptr;
CreateVertexBuffer original_create_vertex_buffer=nullptr;
void* draw_entry=nullptr;
FontLog logger=nullptr;
thread_local bool capturing=false;
thread_local std::uint32_t current_page=0;
struct Upload { std::vector<MapFontVertex> vertices; };
// Borrow buffer identities. Holding a default-pool buffer would block Reset.
std::unordered_map<IDirect3DVertexBuffer9*,std::map<UINT,Upload>> uploads;
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
    for(const auto& upload:found->second) cached_bytes-=upload.second.vertices.size()*sizeof(MapFontVertex);
    uploads.erase(found);
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
        const auto pages=map_font_texture_pages(state.texture.Get());
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
        if(byte_offset>UINT_MAX) throw std::out_of_range("Font vertex offset exceeds the native buffer");
        const auto upload=buffer->second.find(static_cast<UINT>(byte_offset));
        if(upload==buffer->second.end()||upload->second.vertices.size()<vertices)
            throw std::runtime_error("Paged font geometry has no matching CPU upload");
        const auto& source=upload->second.vertices;
        if(std::none_of(source.begin(),source.begin()+vertices,[](const auto& vertex){return vertex.u>=2;}))
            return original_indexed_draw(device,type,base,minimum,vertices,start,primitives);
        std::vector<MapFontVertex> physical(source.begin(),source.begin()+vertices);
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
}
void configure_font_draw(FontLog log) noexcept { logger=log; }
void install_font_draw_device(IDirect3DDevice9* device) {
    const auto table=*reinterpret_cast<void***>(device);
    if(draw_entry==table[82]) return;
    if(draw_entry) throw std::runtime_error("Multiple native font drawing implementations are unsupported");
    if(MH_CreateHook(table[82],reinterpret_cast<void*>(draw_indexed_font),reinterpret_cast<void**>(&original_indexed_draw))!=MH_OK||
       MH_CreateHook(table[26],reinterpret_cast<void*>(create_vertex_buffer),reinterpret_cast<void**>(&original_create_vertex_buffer))!=MH_OK||
       MH_EnableHook(table[26])!=MH_OK||MH_EnableHook(table[82])!=MH_OK)
        throw std::runtime_error("Paged font drawing hook failed");
    draw_entry=table[82];
}
void reset_font_draw_device(IDirect3DDevice9* device) noexcept {
    try { std::lock_guard<std::recursive_mutex> lock(mutex);draw_buffers.erase(device); } catch(...) {}
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
    original_map_geometry(owner,sector,labels,count,font);
}
void upload_map_font_vertices(void* context,void* buffer,const void* data,int vertices,int offset,int mode) {
    if(capturing&&buffer&&data&&vertices>0&&offset>=0) {
        try {
            const auto object=static_cast<const std::byte*>(buffer);
            const auto native=*reinterpret_cast<IDirect3DVertexBuffer9* const*>(object);
            const auto stride=*reinterpret_cast<const int*>(object+8);
            const auto capacity=*reinterpret_cast<const int*>(object+12);
            const auto count=(std::min)(vertices,capacity);
            if(native&&stride==sizeof(MapFontVertex)&&count>0&&count%4==0) {
                std::lock_guard<std::recursive_mutex> lock(mutex);
                auto& slot=uploads[native][static_cast<UINT>(offset)];
                const auto bytes=static_cast<std::size_t>(count)*sizeof(MapFontVertex);
                const auto old_bytes=slot.vertices.size()*sizeof(MapFontVertex);
                if(cached_bytes-old_bytes+bytes>shadow_budget) throw std::length_error("Map vertex CPU cache budget exceeded");
                const auto source=static_cast<const MapFontVertex*>(data);
                slot.vertices.assign(source,source+count);
                cached_bytes=cached_bytes-old_bytes+bytes;
                if(logger&&!logged_upload) { logger("Map font vertex upload captured.");logged_upload=true; }
            }
        } catch(const std::exception& error) { if(logger) logger(error.what()); }
    }
    original_vertex_upload(context,buffer,data,vertices,offset,mode);
}
void mark_map_font_glyph(const NativeGlyph* glyph,MapFontVertex* vertices) noexcept {
    tag_font_vertices(vertices,6,font_glyph_page(glyph));
}
void remember_map_font_glyph(const NativeGlyph* glyph) noexcept { current_page=font_glyph_page(glyph); }
void mark_current_map_font_glyph(MapFontVertex* vertices) noexcept { tag_font_vertices(vertices,6,current_page); }
}
