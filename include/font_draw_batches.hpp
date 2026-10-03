#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace eu4unicode {
struct MapFontVertex { float x,y,z,u,v; };
static_assert(sizeof(MapFontVertex)==20);
struct FontDrawBatch { std::uint32_t page;std::size_t first_quad,quad_count; };
// Page tags travel through the native geometry cache in U, independently of
// the engine's fixed-size glyph records. Decode them before GPU submission.
void tag_font_vertices(MapFontVertex* vertices,std::size_t count,std::uint32_t page) noexcept;
std::vector<FontDrawBatch> split_font_quads(std::vector<MapFontVertex>& vertices,std::size_t pages);
}
