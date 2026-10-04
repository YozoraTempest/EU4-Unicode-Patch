#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eu4unicode {
// A process-local collection and ordered fallback. No installed fonts or
// registry settings are changed. Runs retain the selected file-backed face.
class TextFonts {
public:
    explicit TextFonts(const std::vector<std::filesystem::path>& files,bool system_first=false);
    ~TextFonts();
    TextFonts(const TextFonts&)=delete;
    TextFonts& operator=(const TextFonts&)=delete;
    std::vector<std::string> families() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    friend class TextLayout;
};
class GlyphFace;
enum class GlyphMeasure : std::uint32_t { Natural=0,GdiClassic=1,GdiNatural=2 };
struct GlyphOffset { float advance,ascender; };
struct GlyphCluster {
    std::size_t text_start,text_length;
    std::uint32_t first_glyph,glyph_count;
};
struct GlyphRun {
    std::string font_family;
    std::uint32_t bidi_level;
    std::size_t text_start, text_length;
    std::vector<std::uint16_t> glyphs;
    std::vector<float> advances;
    float baseline_x=0,baseline_y=0,em_size=0;
    bool sideways=false;
    std::vector<GlyphOffset> offsets;
    std::vector<GlyphCluster> clusters;
    // Retains the exact fallback face; a family name cannot identify glyph IDs.
    std::shared_ptr<const GlyphFace> face;
    GlyphMeasure measuring=GlyphMeasure::Natural;
    std::uint32_t style=0;
};
struct GlyphBitmap {
    std::uint32_t width,height;
    // Pixel bounds relative to floor(baseline_x/y), retaining fractional phase.
    // RTL ink may extend left. A blank run has advance but no bitmap allocation.
    std::int32_t left,top;
    std::vector<std::uint8_t> alpha;
};
GlyphBitmap rasterize_glyph_run(const GlyphRun& run);
struct LayoutMetrics { float width,height; std::uint32_t lines; };
enum class TextDirection { LeftToRight,RightToLeft,Automatic };
struct TextStyleRange {
    std::size_t text_start,text_length;
    std::uint32_t style;
};
struct TextInlineObject {
    std::size_t text_start,text_length;
    float width,height,baseline;
    std::uint32_t id;
};
struct InlinePlacement {
    std::size_t text_start,text_length;
    float x,y,width,height;
    std::uint32_t id,style;
    bool rtl;
};
struct LayoutDrawing {
    std::vector<GlyphRun> runs;
    std::vector<InlinePlacement> objects;
};
struct TextLayoutOptions {
    TextDirection direction=TextDirection::LeftToRight;
    bool wrap=true;
    // Zero uses the font's natural spacing. Native UI fonts have a fixed grid.
    float line_height=0;
    std::vector<TextStyleRange> styles;
    std::vector<TextInlineObject> objects;
};
struct LayoutLine {
    std::size_t text_start,text_length,newline_length;
    float width,top,height,baseline;
};
struct HitPosition { std::size_t byte_offset; bool inside; };
struct RasterImage {
    std::uint32_t width,height;
    float baseline;
    // Premultiplied BGRA, with a 16-pixel transparent margin on each side.
    std::vector<std::uint8_t> pixels;
};
// UTF-8 remains the public text format. DirectWrite's UTF-16 positions are
// translated at this boundary; engine byte offsets never become glyph IDs.
class TextLayout {
public:
    TextLayout(std::string_view text,float size,float width,float height,
               std::wstring_view family=L"Segoe UI",
               std::shared_ptr<const TextFonts> fonts={},TextLayoutOptions options={});
    ~TextLayout();
    TextLayout(TextLayout&&) noexcept;
    TextLayout& operator=(TextLayout&&) noexcept;
    TextLayout(const TextLayout&)=delete;
    TextLayout& operator=(const TextLayout&)=delete;
    LayoutMetrics metrics() const;
    std::vector<LayoutLine> lines() const;
    std::vector<GlyphRun> glyph_runs() const;
    LayoutDrawing drawing() const;
    HitPosition hit_test(float x,float y) const;
    // Application effects and inline objects are emitted by drawing(); the
    // standalone PNG renderer handles ordinary text layouts.
    RasterImage rasterize() const;
    void render_png(const std::filesystem::path& path) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
