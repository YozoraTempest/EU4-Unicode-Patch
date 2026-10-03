#pragma once
#include <cstddef>
#include <cstdint>

namespace eu4unicode {
struct NativeGlyph {
    std::int16_t x,y,width,height,x_offset,y_offset,advance;
    std::uint8_t kerning,reserved;
};
static_assert(sizeof(NativeGlyph)==16);
// Font copies share ASCII records. The A-glyph identifies aliases of an atlas.
// The registry owns all higher Unicode glyphs and keeps their pointers stable.
NativeGlyph* allocate_unicode_glyph(void* const* table,std::uint32_t scalar) noexcept;
void* find_unicode_glyph(void* const* table,std::uint32_t scalar) noexcept;
// Bind records loaded before ASCII A becomes available to its atlas identity.
bool bind_unicode_font(void* const* table) noexcept;
// Called before the owning native table destroys its ASCII records.
// Copies that borrowed those native pointers cease to identify a live atlas.
void release_unicode_font(void* const* table) noexcept;
struct GlyphRegistryUsage { std::size_t fonts,glyphs; };
GlyphRegistryUsage unicode_glyph_usage() noexcept;
}
