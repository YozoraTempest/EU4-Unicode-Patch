#pragma once
#include "linux/native.hpp"
#include "glyph_registry.hpp"
#include "font_draw_batches.hpp"
#include "paragraph_transport.hpp"

namespace eu4unicode::linux_native {
struct FontPage { void* texture;FontPageSize size; };
struct FontParagraph {
    std::shared_ptr<const ShapedParagraph> layout;
    std::string text;
    std::uint32_t first_token=0;
    std::vector<NativeGlyph> glyphs;
};
std::shared_ptr<const ShapedParagraph> font_paragraph_layout(void* font,std::string_view text,float width,bool wrap,bool formatted,bool force=false);
std::shared_ptr<const FontParagraph> font_paragraph_geometry(void* font,std::string_view text,float width,bool wrap,bool formatted,const ParagraphViewport* viewport=nullptr);
void prepare_font_pages(Hooks&);
void prepare_font_draw(Hooks&);
void register_font_pages(void* font);
void release_font_pages(void* const* table) noexcept;
NativeGlyph* find_font_glyph(void* const* table,std::uint32_t scalar,const char* origin="unknown") noexcept;
std::uint32_t font_page(const NativeGlyph* glyph) noexcept;
std::vector<FontPage> font_pages(void* original_texture);
void report_font_page_counts() noexcept;
void report_font_draw_counts() noexcept;
void release_font_page_resources();
}
