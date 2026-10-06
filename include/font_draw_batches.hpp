#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace eu4unicode {
struct MapFontVertex { float x,y,z,u,v; };
static_assert(sizeof(MapFontVertex)==20);
struct PopupFontVertex { float x,y,z,u,v;std::uint32_t color,secondary_color; };
static_assert(sizeof(PopupFontVertex)==28);
struct FontDrawBatch { std::uint32_t page;std::size_t first_quad,quad_count; };
struct FontPageSize {
    std::uint32_t width,height;
    std::uint64_t rgba_bytes() const noexcept { return static_cast<std::uint64_t>(width)*height*4; }
};
FontPageSize supplemental_font_page_size(FontPageSize original);
void check_font_page_budget(std::uint64_t supplemental_bytes,std::uint64_t initial_staging_bytes,FontPageSize next);
// Page tags travel through the native geometry cache in U, independently of
// the engine's fixed-size glyph records. Decode them before GPU submission.
void tag_font_vertices(MapFontVertex* vertices,std::size_t count,std::uint32_t page) noexcept;
void tag_font_vertices(PopupFontVertex* vertices,std::size_t count,std::uint32_t page) noexcept;
std::vector<FontDrawBatch> split_font_quads(std::vector<MapFontVertex>& vertices,const std::vector<FontPageSize>& pages);
std::vector<FontDrawBatch> split_popup_glyphs(std::vector<PopupFontVertex>& vertices,const std::vector<FontPageSize>& pages);
}
