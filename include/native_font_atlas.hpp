#pragma once
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>
#include <d3d9.h>
#include <wrl/client.h>
#include "glyph_registry.hpp"
struct IDirect3DTexture9;
struct IDirect3DBaseTexture9;

namespace eu4unicode {
using FontLog=void(*)(const char*);
using NativeTextureLookup=void*(*)(void*,int);
extern NativeTextureLookup original_texture_lookup;
void configure_font_atlases(const std::filesystem::path& assets,const std::filesystem::path& fonts,FontLog log,
                           std::string_view prefix="gfx/fonts/",bool prefer_system_fonts=false);
void register_font_atlas(void* font,std::string_view selected_path={}) noexcept;
NativeGlyph* find_dynamic_glyph(void* const* table,std::uint32_t scalar) noexcept;
void release_font_atlas(void* const* table) noexcept;
void* synchronize_font_texture(void* manager,int id);
bool dynamic_map_font(void* font) noexcept;
std::uint32_t font_glyph_page(const NativeGlyph* glyph) noexcept;
using FontTexturePages=std::vector<Microsoft::WRL::ComPtr<IDirect3DTexture9>>;
FontTexturePages map_font_texture_pages(IDirect3DBaseTexture9* first);
}
