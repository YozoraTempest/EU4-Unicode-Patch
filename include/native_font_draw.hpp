#pragma once
#include "native_font_atlas.hpp"
#include "font_draw_batches.hpp"
struct IDirect3DDevice9;

namespace eu4unicode {
using NativeMapGeometry=void(*)(void*,void*,void*,int,void*);
using NativeVertexUpload=void(*)(void*,void*,const void*,int,int,int);
extern NativeMapGeometry original_map_geometry;
extern NativeVertexUpload original_vertex_upload;
void configure_font_draw(FontLog log) noexcept;
void build_map_font_geometry(void* owner,void* sector,void* labels,int count,void* font);
void upload_map_font_vertices(void* context,void* buffer,const void* data,int vertices,int offset,int mode);
void mark_map_font_glyph(const NativeGlyph* glyph,MapFontVertex* vertices) noexcept;
void remember_map_font_glyph(const NativeGlyph* glyph) noexcept;
void mark_current_map_font_glyph(MapFontVertex* vertices) noexcept;
void install_font_draw_device(IDirect3DDevice9* device);
void reset_font_draw_device(IDirect3DDevice9* device) noexcept;
void release_font_draw_cache() noexcept;
}
