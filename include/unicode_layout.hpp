#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eu4unicode {
struct GlyphRun {
    std::string font_family;
    std::uint32_t bidi_level;
    std::size_t text_start, text_length;
    std::vector<std::uint16_t> glyphs;
    std::vector<float> advances;
};
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
               std::wstring_view family=L"Segoe UI");
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
