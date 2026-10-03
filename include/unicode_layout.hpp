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
    explicit TextFonts(const std::vector<std::filesystem::path>& files);
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
               std::shared_ptr<const TextFonts> fonts={});
    ~TextLayout();
    TextLayout(TextLayout&&) noexcept;
    TextLayout& operator=(TextLayout&&) noexcept;
    TextLayout(const TextLayout&)=delete;
    TextLayout& operator=(const TextLayout&)=delete;
    LayoutMetrics metrics() const;
    std::vector<GlyphRun> glyph_runs() const;
    HitPosition hit_test(float x,float y) const;
    RasterImage rasterize() const;
    void render_png(const std::filesystem::path& path) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
