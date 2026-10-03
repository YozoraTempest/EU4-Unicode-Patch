#pragma once
#include <cstdint>
#include <filesystem>
#include "glyph_registry.hpp"

namespace eu4unicode {
using FontLog=void(*)(const char*);
using NativeTextureLookup=void*(*)(void*,int);
extern NativeTextureLookup original_texture_lookup;
void configure_font_atlases(const std::filesystem::path& fixture,const std::filesystem::path& fonts,FontLog log);
void register_font_atlas(void* font) noexcept;
NativeGlyph* find_dynamic_glyph(void* const* table,std::uint32_t scalar) noexcept;
void release_font_atlas(void* const* table) noexcept;
void* synchronize_font_texture(void* manager,int id);
}
