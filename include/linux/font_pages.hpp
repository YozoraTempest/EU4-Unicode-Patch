#pragma once
#include "linux/native.hpp"
#include "glyph_registry.hpp"
#include "font_draw_batches.hpp"

namespace eu4unicode::linux_native {
struct FontPage { void* texture;FontPageSize size; };
void prepare_font_pages(Hooks&);
void prepare_font_draw(Hooks&);
void register_font_pages(void* font);
void release_font_pages(void* const* table) noexcept;
NativeGlyph* find_font_glyph(void* const* table,std::uint32_t scalar) noexcept;
std::uint32_t font_page(const NativeGlyph* glyph) noexcept;
std::vector<FontPage> font_pages(void* original_texture);
void report_font_page_counts() noexcept;
void report_font_draw_counts() noexcept;
void release_font_page_resources();
}
