#include "native_font_atlas.hpp"
#include "scalar_glyph.hpp"
#include <windows.h>
#include <d3d9.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <array>
#include <cstring>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

namespace {
using Microsoft::WRL::ComPtr;
struct TextureWrapper { IDirect3DTexture9* texture=nullptr; bool srgb=false; };
DWORD graphics_thread=0;
void* lookup(void* manager,int id) {
    if(GetCurrentThreadId()!=graphics_thread) throw std::runtime_error("CPU glyph lookup accessed the graphics manager from a different thread");
    if(id!=1) return nullptr;return manager;
}
void checked(HRESULT status) { if(FAILED(status)) throw std::runtime_error("GPU test failed: "+std::to_string(static_cast<unsigned long>(status))); }
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void log(const char* value) { std::cout<<value<<'\n'; }
std::vector<std::uint8_t> readback(IDirect3DDevice9* device,IDirect3DTexture9* texture,const eu4unicode::NativeGlyph& glyph) {
    ComPtr<IDirect3DSurface9> source,target,system;
    checked(texture->GetSurfaceLevel(0,&source));
    checked(device->CreateRenderTarget(glyph.width,glyph.height,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    const RECT rect{glyph.x,glyph.y,glyph.x+glyph.width,glyph.y+glyph.height};
    checked(device->StretchRect(source.Get(),&rect,target.Get(),nullptr,D3DTEXF_NONE));
    checked(device->CreateOffscreenPlainSurface(glyph.width,glyph.height,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&system,nullptr));
    checked(device->GetRenderTargetData(target.Get(),system.Get()));
    D3DLOCKED_RECT pixels{};checked(system->LockRect(&pixels,nullptr,D3DLOCK_READONLY));
    std::vector<std::uint8_t> result;
    for(int y=0;y<glyph.height;++y) for(int x=0;x<glyph.width;++x)
        result.push_back(static_cast<std::uint8_t*>(pixels.pBits)[y*pixels.Pitch+x*4+3]);
    checked(system->UnlockRect());return result;
}
}
int wmain(int argc,wchar_t** argv) {
    HWND window=nullptr;
    try {
        if(argc!=3&&argc!=4) throw std::invalid_argument("Usage: native_font_atlas_tests ASSET_DIRECTORY FONT_DIRECTORY [--player]");
        const bool player=argc==4&&std::wstring_view(argv[3])==L"--player";
        if(argc==4&&!player) throw std::invalid_argument("Unknown atlas test option");
        require(MH_Initialize()==MH_OK,"MinHook initialization failed");
        WNDCLASSW type{};type.lpfnWndProc=DefWindowProcW;type.hInstance=GetModuleHandleW(nullptr);type.lpszClassName=L"EU4UnicodeFontGpuTests";
        require(RegisterClassW(&type)!=0,"Cannot register GPU test window");
        window=CreateWindowW(type.lpszClassName,L"EU4 font GPU tests",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,type.hInstance,nullptr);
        require(window!=nullptr,"Cannot create hidden GPU test window");
        ComPtr<IDirect3D9> api;api.Attach(Direct3DCreate9(D3D_SDK_VERSION));require(api!=nullptr,"Direct3D9 unavailable");
        D3DPRESENT_PARAMETERS parameters{};parameters.Windowed=TRUE;parameters.SwapEffect=D3DSWAPEFFECT_DISCARD;
        parameters.hDeviceWindow=window;parameters.BackBufferWidth=64;parameters.BackBufferHeight=64;parameters.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        ComPtr<IDirect3DDevice9> device;
        checked(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&parameters,&device));
        ComPtr<IDirect3DTexture9> texture;
        checked(device->CreateTexture(2048,4096,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr));
        TextureWrapper wrapper{texture.Get()};
        alignas(void*) std::array<std::byte,0x980> object{};
        alignas(void*) std::array<std::byte,0x488> context{};
        auto f=object.data();auto table=reinterpret_cast<void**>(f+0x120);
        eu4unicode::NativeGlyph anchor{};table[0x41]=&anchor;
        *reinterpret_cast<std::byte**>(f+0x48)=context.data();
        *reinterpret_cast<void**>(context.data()+0x480)=&wrapper;
        const char* path=player?"gfx/fonts/eu4-unicode/zh-hans-16":"gfx/fonts/zh-hans-16";
        *reinterpret_cast<const char**>(f+0xe0)=path;
        *reinterpret_cast<std::uint64_t*>(f+0xf0)=std::strlen(path);
        *reinterpret_cast<std::uint64_t*>(f+0xf8)=31;
        *reinterpret_cast<int*>(f+0x970)=1;*reinterpret_cast<int*>(f+0x978)=2048;*reinterpret_cast<int*>(f+0x97c)=4096;
        eu4unicode::original_texture_lookup=lookup;
        graphics_thread=GetCurrentThreadId();
        eu4unicode::configure_font_atlases(argv[1],argv[2],log,player?"gfx/fonts/eu4-unicode/":"gfx/fonts/");
        eu4unicode::register_font_atlas(f);
        const std::filesystem::path fonts_directory(argv[2]);
        auto fonts=std::make_shared<eu4unicode::TextFonts>(std::vector<std::filesystem::path>{fonts_directory/L"SourceHanSansSC-Regular.otf",
            fonts_directory/L"PlangothicP1-Regular.ttf",fonts_directory/L"PlangothicP2-Regular.ttf"});
        auto task=std::async(std::launch::async,[table]{return eu4unicode::find_dynamic_glyph(table,0x5b54);});
        auto glyph=task.get();
        require(glyph!=nullptr,"On-demand native glyph was not allocated");
        eu4unicode::synchronize_font_texture(&wrapper,1);
        const auto reference=eu4unicode::rasterize_scalar(0x5b54,16,fonts);
        require(readback(device.Get(),texture.Get(),*glyph)==reference.alpha,"First native GPU upload differs from its raster");
        auto supplementary=eu4unicode::find_dynamic_glyph(table,0x30000);
        require(supplementary!=nullptr,"Supplementary native glyph missing");
        eu4unicode::synchronize_font_texture(&wrapper,1);
        require(readback(device.Get(),texture.Get(),*glyph)==reference.alpha,"Second upload damaged the first region");
        auto previous=texture.Get();wrapper.texture=nullptr;texture.Reset();
        checked(device->Reset(&parameters));
        checked(device->CreateTexture(2048,4096,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr));
        wrapper.texture=texture.Get();
        eu4unicode::synchronize_font_texture(&wrapper,1);
        require(readback(device.Get(),texture.Get(),*glyph)==reference.alpha,"Device reset lost the cached glyph region");
        require(readback(device.Get(),texture.Get(),*supplementary)==eu4unicode::rasterize_scalar(0x30000,16,fonts).alpha,"Device reset lost the supplementary glyph");
        require(eu4unicode::find_dynamic_glyph(table,0x5b54)==glyph,"Native glyph pointer changed after reset");
        std::cout<<"Device reset restored both cached GPU regions; native pointer reuse="<<(previous==texture.Get())<<".\n";
        eu4unicode::release_font_atlas(table);eu4unicode::release_unicode_font(table);
        require(eu4unicode::find_dynamic_glyph(table,0x5b54)==nullptr,"Released font atlas remains reachable");
        require(eu4unicode::unicode_glyph_usage().glyphs==0,"Released glyph registry leaked records");
        if(player) {
            for(const auto size:{14,18,24,88}) {
                const auto size_path=std::string("gfx/fonts/eu4-unicode/zh-hans-")+(size==88?"map":std::to_string(size));
                *reinterpret_cast<const char**>(f+0xe0)=size_path.c_str();
                *reinterpret_cast<std::uint64_t*>(f+0xf0)=size_path.size();
                eu4unicode::register_font_atlas(f);
                for(const auto scalar:{0x4e2du,0x30000u}) {
                    auto record=eu4unicode::find_dynamic_glyph(table,scalar);
                    require(record!=nullptr,"Player size has no dynamic glyph");
                    eu4unicode::synchronize_font_texture(&wrapper,1);
                    require(readback(device.Get(),texture.Get(),*record)==eu4unicode::rasterize_scalar(scalar,size,fonts).alpha,
                            "Player UI/map atlas GPU region differs from its raster");
                }
                eu4unicode::release_font_atlas(table);eu4unicode::release_unicode_font(table);
            }
            require(eu4unicode::unicode_glyph_usage().glyphs==0,"Player size loop leaked records");
            std::cout<<"Player assets: all five UI/map sizes match real D3D9 texture pixels.\n";
        }
        require(MH_Uninitialize()==MH_OK,"MinHook cleanup failed");
        std::cout<<"Native GPU upload, preserved region, supplementary glyph, device reset, stable pointer and font release passed.\n";
        DestroyWindow(window);return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';MH_Uninitialize();if(window) DestroyWindow(window);return 1;
    }
}
