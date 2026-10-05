#pragma once
#include "unicode_layout.hpp"
#include <mutex>

namespace eu4unicode {
class ParagraphText;
// Select contextual scripts, combining sequences and bidi controls. Ordinary
// Chinese/Latin text keeps its existing native font metrics.
bool needs_paragraph_shaping(std::string_view text) noexcept;
bool plain_native_paragraph(std::string_view text) noexcept;
struct ParagraphTile {
    GlyphBitmap bitmap;
    float x,y;
    std::size_t line,text_start,text_length;
    std::uint32_t style=0;
};
class ShapedParagraph {
public:
    ShapedParagraph(std::string_view text,int size,float width,bool wrap,
                    std::shared_ptr<const TextFonts> fonts={},std::shared_ptr<const ParagraphText> content={});
    const std::string& text() const noexcept { return text_; }
    const LayoutMetrics& metrics() const noexcept { return metrics_; }
    const std::vector<LayoutLine>& lines() const noexcept { return lines_; }
    const std::vector<GlyphRun>& runs() const noexcept { return runs_; }
    const std::vector<InlinePlacement>& objects() const noexcept { return objects_; }
    const ParagraphText& content() const noexcept { return *content_; }
    HitPosition hit_test(float x,float y) const;
    CaretPosition caret(std::size_t byte_offset,bool trailing=false) const;
    CaretPosition move_caret(std::size_t byte_offset,bool trailing,bool right) const;
    std::vector<SelectionRegion> selection(std::size_t begin,std::size_t end) const;
    bool missing_glyphs() const noexcept;
private:
    struct Block {
        std::size_t start,length;
        float top,height;
        std::unique_ptr<TextLayout> layout;
    };
    std::string text_;
    LayoutMetrics metrics_{};
    std::vector<LayoutLine> lines_;
    std::vector<GlyphRun> runs_;
    std::vector<InlinePlacement> objects_;
    std::vector<Block> blocks_;
    std::shared_ptr<const ParagraphText> content_;
    bool missing_=false;
    mutable std::once_flag caret_stops_once_;
    mutable std::vector<CaretPosition> caret_stops_;
};
// Tile after shaping, retaining the exact selected face, glyph positions and
// fractional raster phase. Texture page boundaries never become text breaks.
std::vector<ParagraphTile> rasterize_paragraph(const ShapedParagraph& paragraph,
                                              std::uint32_t width,std::uint32_t height);
}
