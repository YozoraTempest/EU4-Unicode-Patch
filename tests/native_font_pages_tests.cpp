#include "native_font_atlas.hpp"
#include "native_font_draw.hpp"
#include "scalar_glyph.hpp"
#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using Microsoft::WRL::ComPtr;
using eu4unicode::MapFontVertex;
struct TextureWrapper { IDirect3DTexture9* texture=nullptr; };
struct VertexWrapper { IDirect3DVertexBuffer9* buffer;int stride,capacity;unsigned flags=0; };
enum class DrawPath { indexed,popup,ui_upload,ui_create,ui_partial,ui_dynamic,ui_managed };
struct CreateContext {
    IDirect3DDevice9* device;
    ComPtr<IDirect3DVertexBuffer9> buffer;
    VertexWrapper wrapper{};
};
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void checked_status(HRESULT value,const char* operation) {
    if(FAILED(value)) throw std::runtime_error(std::string(operation)+" failed: "+std::to_string(static_cast<unsigned long>(value)));
}
#define checked(operation) checked_status((operation),#operation)
void log(const char* value) {
    if(std::strstr(value,"page")||std::strstr(value,"failed")||std::strstr(value,"exhausted")||std::strstr(value,"matching")||std::strstr(value,"rejected")) std::cout<<value<<'\n';
}
void* texture_lookup(void* manager,int) { return manager; }
void vertex_upload(void*,void* object,const void* data,int count,int offset,int mode) {
    const auto wrapper=static_cast<VertexWrapper*>(object);
    count=count<0?wrapper->capacity:(std::min)(count,wrapper->capacity);
    const DWORD flags=(wrapper->flags&0x200)?(mode==1?D3DLOCK_NOOVERWRITE:D3DLOCK_DISCARD):0;
    void* target=nullptr;checked(wrapper->buffer->Lock(offset,count*wrapper->stride,&target,flags));
    std::memcpy(target,data,count*wrapper->stride);checked(wrapper->buffer->Unlock());
}
void* vertex_create(void* object,const void* data,int count,int stride,bool,const void*) {
    const auto context=static_cast<CreateContext*>(object);
    checked(context->device->CreateVertexBuffer(count*stride,D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&context->buffer,nullptr));
    context->wrapper={context->buffer.Get(),stride,count};
    if(data) vertex_upload(nullptr,&context->wrapper,data,count,0,0);
    return &context->wrapper;
}
void vertex_release(void* object) {
    if(object&&static_cast<VertexWrapper*>(object)->buffer) static_cast<VertexWrapper*>(object)->buffer->Release();
}
void geometry(void* owner,void*,void* labels,int count,void*) {
    eu4unicode::upload_map_font_vertices(nullptr,owner,labels,count,0,0);
}
std::filesystem::path ui_fixture(const std::filesystem::path& assets,int size) {
    const auto root=assets/"ui-page-tests";
    const auto relative=std::filesystem::path("gfx/fonts/eu4-unicode/cache")/("zh-hans-"+std::to_string(size));
    const auto stem=root/relative;
    std::filesystem::create_directories(stem.parent_path());
    std::filesystem::copy_file(assets/(relative.string()+".dds"),stem.string()+".dds",std::filesystem::copy_options::overwrite_existing);
    // Reserve the first page up to its final 64 rows, so the test reaches a
    // real page boundary at each UI size without rasterizing the entire BMP.
    std::ofstream metrics(stem.string()+".fnt");
    metrics<<"common lineHeight="<<size<<" scaleW=2048 scaleH=4096 pages=1\n"
           <<"char id=65 x=1 y=4030 width=1 height=1\n";
    require(static_cast<bool>(metrics),"Cannot create the UI page-boundary fixture");
    return root;
}
std::vector<std::uint8_t> read_surface(IDirect3DDevice9* device,IDirect3DSurface9* source,const RECT& rect) {
    D3DSURFACE_DESC desc{};checked(source->GetDesc(&desc));
    ComPtr<IDirect3DSurface9> system;
    checked(device->CreateOffscreenPlainSurface(desc.Width,desc.Height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&system,nullptr));
    checked(device->GetRenderTargetData(source,system.Get()));
    D3DLOCKED_RECT pixels{};checked(system->LockRect(&pixels,&rect,D3DLOCK_READONLY));
    std::vector<std::uint8_t> result;
    for(int y=0;y<rect.bottom-rect.top;++y) for(int x=0;x<rect.right-rect.left;++x)
        result.push_back(static_cast<const std::uint8_t*>(pixels.pBits)[y*pixels.Pitch+x*4+3]);
    checked(system->UnlockRect());return result;
}
std::vector<std::uint8_t> read_glyph(IDirect3DDevice9* device,IDirect3DTexture9* texture,const eu4unicode::NativeGlyph& glyph) {
    ComPtr<IDirect3DSurface9> source,target;
    checked(texture->GetSurfaceLevel(0,&source));
    checked(device->CreateRenderTarget(glyph.width,glyph.height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    const RECT rect{glyph.x,glyph.y,glyph.x+glyph.width,glyph.y+glyph.height};
    checked(device->StretchRect(source.Get(),&rect,target.Get(),nullptr,D3DTEXF_NONE));
    return read_surface(device,target.Get(),{0,0,glyph.width,glyph.height});
}
void draw_glyphs(IDirect3DDevice9* device,void* font,IDirect3DTexture9* first,
                 const eu4unicode::NativeGlyph& a,const eu4unicode::NativeGlyph& b,
                 const std::vector<std::uint8_t>& alpha_a,const std::vector<std::uint8_t>& alpha_b,DrawPath path=DrawPath::indexed,
                 ComPtr<IDirect3DVertexBuffer9>* retained_buffer=nullptr) {
    const bool popup=path!=DrawPath::indexed,ui=path!=DrawPath::indexed&&path!=DrawPath::popup;
    constexpr UINT width=384,height=128;
    ComPtr<IDirect3DSurface9> saved,target;
    checked(device->GetRenderTarget(0,&saved));
    checked(device->CreateRenderTarget(width,height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    checked(device->SetRenderTarget(0,target.Get()));
    checked(device->SetDepthStencilSurface(nullptr));
    D3DVIEWPORT9 viewport{0,0,width,height,0,1};checked(device->SetViewport(&viewport));
    checked(device->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
    D3DMATRIX identity{};identity._11=identity._22=identity._33=identity._44=1;
    for(const auto transform:{D3DTS_WORLD,D3DTS_VIEW,D3DTS_PROJECTION}) checked(device->SetTransform(transform,&identity));
    checked(device->SetVertexShader(nullptr));checked(device->SetPixelShader(nullptr));
    checked(device->SetFVF(D3DFVF_XYZ|D3DFVF_TEX1));
    for(const auto state:{D3DRS_LIGHTING,D3DRS_ZENABLE,D3DRS_FOGENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE})
        checked(device->SetRenderState(state,FALSE));
    checked(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    checked(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
    checked(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE));
    checked(device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1));
    checked(device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE));
    checked(device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE));
    for(const auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER}) checked(device->SetSamplerState(0,state,D3DTEXF_POINT));
    checked(device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE));
    std::vector<MapFontVertex> vertices;
    const std::array<const eu4unicode::NativeGlyph*,3> glyphs{{&a,&b,&a}};
    std::array<RECT,3> regions{};
    for(std::size_t i=0;i<glyphs.size();++i) {
        const auto& g=*glyphs[i];const auto left=8+static_cast<int>(i)*120,top=8;
        regions[i]={left,top,left+g.width,top+g.height};
        const auto x0=-1+2*(left-0.5f)/width,x1=-1+2*(left+g.width-0.5f)/width;
        const auto y0=1-2*(top-0.5f)/height,y1=1-2*(top+g.height-0.5f)/height;
        const auto u0=g.x/2048.0f,u1=(g.x+g.width)/2048.0f;
        const auto v0=g.y/4096.0f,v1=(g.y+g.height)/4096.0f;
        const auto begin=vertices.size();
        vertices.insert(vertices.end(),{{x0,y0,0.5f,u0,v0},{x1,y0,0.5f,u1,v0},{x1,y1,0.5f,u1,v1},{x0,y1,0.5f,u0,v1}});
        eu4unicode::tag_font_vertices(vertices.data()+begin,4,i==1?eu4unicode::font_glyph_page(&b):0);
    }
    ComPtr<IDirect3DVertexBuffer9> buffer;
    checked(device->CreateVertexBuffer(static_cast<UINT>(vertices.size()*sizeof(MapFontVertex)),D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&buffer,nullptr));
    VertexWrapper wrapper{buffer.Get(),sizeof(MapFontVertex),static_cast<int>(vertices.size())};
    eu4unicode::build_map_font_geometry(&wrapper,nullptr,vertices.data(),static_cast<int>(vertices.size()),font);
    const std::array<WORD,18> indices{{0,1,2,2,3,0,4,5,6,6,7,4,8,9,10,10,11,8}};
    ComPtr<IDirect3DIndexBuffer9> index;
    checked(device->CreateIndexBuffer(sizeof(indices),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&index,nullptr));
    void* output=nullptr;checked(index->Lock(0,sizeof(indices),&output,0));
    std::memcpy(output,indices.data(),sizeof(indices));checked(index->Unlock());
    ComPtr<IDirect3DVertexDeclaration9> declaration;
    CreateContext create_context{device};
    UINT stream_offset=0,start_vertex=0;
    std::vector<eu4unicode::PopupFontVertex> triangles;
    if(popup) {
        for(const auto vertex:indices) {
            const auto& v=vertices[vertex];
            triangles.push_back({v.x,v.y,v.z,v.u-std::floor(v.u/2)*2,v.v,0xffffffff,0xffffffff});
        }
        for(std::size_t i=0;i<glyphs.size();++i)
            eu4unicode::mark_popup_font_glyph(glyphs[i],triangles.data()+i*6);
        if(ui) {
            // Exercise a draw beginning inside a cached upload, with both a
            // nonzero stream offset and nonzero StartVertex.
            triangles.insert(triangles.begin(),6,{0,0,0,0,0,0,0});
            start_vertex=6;
        }
        buffer.Reset();
        if(path==DrawPath::ui_create) {
            eu4unicode::begin_popup_font(font);
            auto created=static_cast<VertexWrapper*>(eu4unicode::create_font_vertices(&create_context,triangles.data(),static_cast<int>(triangles.size()),sizeof(eu4unicode::PopupFontVertex),false,nullptr));
            eu4unicode::end_popup_font();
            buffer=created->buffer;
        } else {
            stream_offset=ui?6*sizeof(eu4unicode::PopupFontVertex):0;
            if(retained_buffer&&*retained_buffer) buffer=*retained_buffer;
            else {
                const bool dynamic=path==DrawPath::ui_dynamic;
                checked(device->CreateVertexBuffer(stream_offset+static_cast<UINT>(triangles.size()*sizeof(eu4unicode::PopupFontVertex)),D3DUSAGE_WRITEONLY|(dynamic?D3DUSAGE_DYNAMIC:0),0,path==DrawPath::ui_managed?D3DPOOL_MANAGED:D3DPOOL_DEFAULT,&buffer,nullptr));
                VertexWrapper popup_wrapper{buffer.Get(),sizeof(eu4unicode::PopupFontVertex),static_cast<int>(triangles.size()),dynamic?0x200u:0u};
                eu4unicode::begin_popup_font(font);
                eu4unicode::upload_map_font_vertices(nullptr,&popup_wrapper,triangles.data(),ui?-1:static_cast<int>(triangles.size()),static_cast<int>(stream_offset),0);
                eu4unicode::end_popup_font();
                if(path==DrawPath::ui_partial||path==DrawPath::ui_dynamic)
                    eu4unicode::upload_map_font_vertices(nullptr,&popup_wrapper,triangles.data()+12,6,static_cast<int>(stream_offset+12*sizeof(eu4unicode::PopupFontVertex)),1);
                if(retained_buffer) *retained_buffer=buffer;
            }
        }
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,12,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
            {0,20,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},
            {0,24,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,1},D3DDECL_END()};
        checked(device->CreateVertexDeclaration(elements,&declaration));checked(device->SetVertexDeclaration(declaration.Get()));
    }
    const auto vertex_stride=popup?sizeof(eu4unicode::PopupFontVertex):sizeof(MapFontVertex);
    checked(device->SetIndices(index.Get()));checked(device->SetStreamSource(0,buffer.Get(),stream_offset,static_cast<UINT>(vertex_stride)));
    checked(device->SetTexture(0,first));
    checked(device->BeginScene());
    if(popup) checked(device->DrawPrimitive(D3DPT_TRIANGLELIST,start_vertex,6));
    else checked(device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,12,0,6));
    checked(device->EndScene());
    ComPtr<IDirect3DVertexBuffer9> restored_buffer;ComPtr<IDirect3DBaseTexture9> restored_texture;ComPtr<IDirect3DIndexBuffer9> restored_index;
    UINT offset=0,stride=0;checked(device->GetStreamSource(0,&restored_buffer,&offset,&stride));
    checked(device->GetTexture(0,&restored_texture));checked(device->GetIndices(&restored_index));
    require(restored_buffer.Get()==buffer.Get()&&offset==stream_offset&&stride==vertex_stride&&restored_texture.Get()==first&&restored_index.Get()==index.Get(),
            "Paged draw changed the engine stream, texture or indices");
    for(std::size_t i=0;i<regions.size();++i)
        require(read_surface(device,target.Get(),regions[i])==(i==1?alpha_b:alpha_a),"Paged font drawing differs from the glyph raster");
    if(path==DrawPath::ui_partial||path==DrawPath::ui_dynamic||path==DrawPath::ui_create) {
        // A later unpaged upload must replace the shadow too. Otherwise an old
        // page tag would silently select the previous page for this buffer.
        for(auto& vertex:triangles) vertex.u-=std::floor(vertex.u/2)*2;
        VertexWrapper overwrite_wrapper{buffer.Get(),sizeof(eu4unicode::PopupFontVertex),static_cast<int>(triangles.size()),path==DrawPath::ui_dynamic?0x200u:0u};
        if(path==DrawPath::ui_create) {
            // The device still references this resource after its engine
            // wrapper is released. Its old CPU page tags must be gone.
            create_context.buffer.Detach();
            eu4unicode::release_font_vertices(&create_context.wrapper);
            vertex_upload(nullptr,&overwrite_wrapper,triangles.data(),-1,static_cast<int>(stream_offset),0);
        } else eu4unicode::upload_map_font_vertices(nullptr,&overwrite_wrapper,triangles.data(),-1,static_cast<int>(stream_offset),0);
        checked(device->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
        checked(device->BeginScene());checked(device->DrawPrimitive(D3DPT_TRIANGLELIST,start_vertex,6));checked(device->EndScene());
        require(read_surface(device,target.Get(),regions[1])==read_glyph(device,first,b),"UI buffer reused stale page tags after an unpaged upload");
    }
    checked(device->SetTexture(0,nullptr));checked(device->SetStreamSource(0,nullptr,0,0));checked(device->SetIndices(nullptr));
    checked(device->SetRenderTarget(0,saved.Get()));
}
}
int wmain(int argc,wchar_t** argv) {
    HWND window=nullptr;
    try {
        require(argc==2||(argc==3&&std::wstring_view(argv[2])==L"--full-ui"),"Usage: native_font_pages_tests ASSET_DIRECTORY [--full-ui]");
        const bool full_ui=argc==3;
        require(MH_Initialize()==MH_OK,"MinHook initialization failed");
        WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"EU4UnicodeFontPagesTests";
        require(RegisterClassW(&type)!=0,"Cannot register GPU test window");
        window=CreateWindowW(type.lpszClassName,L"EU4 font pages tests",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,type.hInstance,nullptr);
        require(window!=nullptr,"Cannot create GPU test window");
        ComPtr<IDirect3D9> api;api.Attach(Direct3DCreate9(D3D_SDK_VERSION));require(api!=nullptr,"Direct3D9 unavailable");
        D3DPRESENT_PARAMETERS parameters{};parameters.Windowed=TRUE;parameters.SwapEffect=D3DSWAPEFFECT_DISCARD;
        parameters.hDeviceWindow=window;parameters.BackBufferWidth=64;parameters.BackBufferHeight=64;
        ComPtr<IDirect3DDevice9> device;
        checked(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&parameters,&device));
        ComPtr<IDirect3DTexture9> texture;
        checked(device->CreateTexture(2048,4096,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr));
        TextureWrapper wrapper{texture.Get()};
        alignas(void*) std::array<std::byte,0x980> object{};
        alignas(void*) std::array<std::byte,0x488> context{};
        auto font=object.data();auto table=reinterpret_cast<void**>(font+0x120);
        eu4unicode::NativeGlyph anchor{};table[0x41]=&anchor;
        *reinterpret_cast<std::byte**>(font+0x48)=context.data();*reinterpret_cast<void**>(context.data()+0x480)=&wrapper;
        *reinterpret_cast<int*>(font+0x970)=1;*reinterpret_cast<int*>(font+0x978)=2048;*reinterpret_cast<int*>(font+0x97c)=4096;
        eu4unicode::original_texture_lookup=texture_lookup;
        eu4unicode::original_map_geometry=geometry;eu4unicode::original_vertex_upload=vertex_upload;
        eu4unicode::original_vertex_create=vertex_create;
        eu4unicode::original_vertex_release=vertex_release;
        const auto sizes=full_ui?std::vector<int>{24}:std::vector<int>{88,14,16,18,24};
        for(const auto size:sizes) {
            const auto assets=size==88||full_ui?std::filesystem::path(argv[1]):ui_fixture(argv[1],size);
            const auto path=std::string("gfx/fonts/eu4-unicode/cache/")+(size==88?"zh-hans-map":"zh-hans-"+std::to_string(size));
            eu4unicode::configure_font_atlases(assets,std::filesystem::path{},log,"gfx/fonts/eu4-unicode/cache/",true);
            eu4unicode::register_font_atlas(font,path);
            require(eu4unicode::dynamic_font(font)&&eu4unicode::dynamic_map_font(font)==(size==88),"Generated UI/map font classification failed");
            auto first=eu4unicode::find_dynamic_glyph(table,0x4e2d);
            require(first&&eu4unicode::font_glyph_page(first)==0,"First glyph is not on the initial page");
            eu4unicode::synchronize_font_texture(&wrapper,1);
            const auto reference=eu4unicode::rasterize_scalar(0x4e2d,size).alpha;
            require(read_glyph(device.Get(),texture.Get(),*first)==reference,"First page upload differs from the glyph raster");
            eu4unicode::NativeGlyph* second=nullptr;std::uint32_t second_scalar=0;
            const auto glyph_count=full_ui?20000u:(size==88?2000u:600u);
            for(std::uint32_t scalar=0x4e00;scalar<0x4e00+glyph_count;++scalar) {
                auto glyph=eu4unicode::find_dynamic_glyph(table,scalar);
                require(glyph!=nullptr,"A supported CJK glyph was lost during multi-page allocation");
                if(!second&&eu4unicode::font_glyph_page(glyph)>0) { second=glyph;second_scalar=scalar; }
            }
            require(second!=nullptr,"Glyphs did not allocate an additional page");
            eu4unicode::synchronize_font_texture(&wrapper,1);
            auto pages=eu4unicode::font_texture_pages(texture.Get());
            require(pages.size()>=2,"Additional GPU page missing");
            const auto second_reference=eu4unicode::rasterize_scalar(second_scalar,size).alpha;
            require(read_glyph(device.Get(),pages[0].Get(),*first)==reference,"Growing the atlas changed the first page");
            require(read_glyph(device.Get(),pages.at(eu4unicode::font_glyph_page(second)).Get(),*second)==second_reference,"Additional page upload differs from its raster");
            if(size==88) draw_glyphs(device.Get(),font,texture.Get(),*first,*second,reference,second_reference);
            for(const auto draw:{DrawPath::popup,DrawPath::ui_upload,DrawPath::ui_create,DrawPath::ui_partial,DrawPath::ui_dynamic})
                draw_glyphs(device.Get(),font,texture.Get(),*first,*second,reference,second_reference,draw);
            ComPtr<IDirect3DVertexBuffer9> retained_buffer;
            draw_glyphs(device.Get(),font,texture.Get(),*first,*second,reference,second_reference,DrawPath::ui_managed,&retained_buffer);
            pages.clear();wrapper.texture=nullptr;texture.Reset();
            checked(device->Reset(&parameters));
            checked(device->CreateTexture(2048,4096,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr));wrapper.texture=texture.Get();
            eu4unicode::synchronize_font_texture(&wrapper,1);pages=eu4unicode::font_texture_pages(texture.Get());
            require(read_glyph(device.Get(),pages[0].Get(),*first)==reference,"Reset lost the first page");
            require(read_glyph(device.Get(),pages.at(eu4unicode::font_glyph_page(second)).Get(),*second)==second_reference,"Reset lost the additional page");
            require(eu4unicode::find_dynamic_glyph(table,0x4e2d)==first&&eu4unicode::find_dynamic_glyph(table,second_scalar)==second,"Reset changed native glyph pointers");
            if(size==88) draw_glyphs(device.Get(),font,texture.Get(),*first,*second,reference,second_reference);
            for(const auto draw:{DrawPath::popup,DrawPath::ui_upload,DrawPath::ui_create,DrawPath::ui_partial,DrawPath::ui_dynamic})
                draw_glyphs(device.Get(),font,texture.Get(),*first,*second,reference,second_reference,draw);
            draw_glyphs(device.Get(),font,texture.Get(),*first,*second,reference,second_reference,DrawPath::ui_managed,&retained_buffer);
            auto alias_object=object;auto alias_table=reinterpret_cast<void**>(alias_object.data()+0x120);
            eu4unicode::NativeGlyph alias_anchor{};alias_table[0x41]=&alias_anchor;
            eu4unicode::register_font_atlas(alias_object.data(),path);
            auto alias_glyph=eu4unicode::find_dynamic_glyph(alias_table,second_scalar);
            require(alias_glyph&&eu4unicode::font_glyph_page(alias_glyph)==eu4unicode::font_glyph_page(second),"Shared UI font lost the glyph page");
            const auto count=pages.size();eu4unicode::release_font_atlas(table);eu4unicode::release_unicode_font(table);
            require(eu4unicode::font_texture_pages(texture.Get()).size()==count,"Releasing one font alias destroyed shared pages");
            draw_glyphs(device.Get(),alias_object.data(),texture.Get(),*eu4unicode::find_dynamic_glyph(alias_table,0x4e2d),*alias_glyph,reference,second_reference,DrawPath::ui_create);
            eu4unicode::release_font_atlas(alias_table);eu4unicode::release_unicode_font(alias_table);
            require(eu4unicode::font_texture_pages(texture.Get()).empty(),"Released atlas still exposes pages");
            require(eu4unicode::unicode_glyph_usage().glyphs==0,"Released paged atlas leaked glyph records");
            pages.clear();wrapper.texture=nullptr;texture.Reset();
            checked(device->CreateTexture(2048,4096,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr));wrapper.texture=texture.Get();
            std::cout<<"PASS size="<<size<<" glyphs="<<glyph_count<<" pages="<<count<<": exact pixels, UI upload/create, static/dynamic overwrite, managed cache reset, buffer/alias release.\n";
        }
        eu4unicode::reset_font_draw_device(device.Get());
        require(MH_Uninitialize()==MH_OK,"MinHook cleanup failed");
        std::cout<<(full_ui?"PASS: 20000 CJK glyphs in the default 24px UI atlas.\n":"PASS: map and all four UI sizes render mixed pages and retain native glyph records through device reset.\n");
        DestroyWindow(window);return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';MH_Uninitialize();if(window) DestroyWindow(window);return 1;
    }
}
