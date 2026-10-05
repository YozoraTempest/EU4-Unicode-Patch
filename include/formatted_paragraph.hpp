#pragma once
#include "unicode_layout.hpp"
#include <functional>

namespace eu4unicode {
struct ParagraphIcon {
    std::size_t text_start,text_length;
    std::string command;
    float advance;
};
// Native color stacks and icon commands remain metadata. DirectWrite receives
// one logical paragraph, with one object replacement character per icon.
class ParagraphText {
public:
    using IconMeasure=std::function<float(std::string_view)>;
    using ColorLookup=std::function<bool(unsigned char)>;
    // A negative result leaves a cluster to DirectWrite. Present bitmap glyphs
    // retain the mod's advance and are emitted through its original renderer.
    using BitmapMeasure=std::function<float(std::uint32_t)>;
    ParagraphText(std::string_view source,bool formatted=true,IconMeasure icons={},ColorLookup colors={},IconMeasure flags={},BitmapMeasure bitmap={},IconMeasure symbols={});
    const std::string& source() const noexcept { return source_; }
    const std::string& visible() const noexcept { return visible_; }
    const std::vector<TextStyleRange>& styles() const noexcept { return styles_; }
    const std::vector<std::string>& colors() const noexcept { return colors_; }
    const std::vector<ParagraphIcon>& icons() const noexcept { return icons_; }
    std::size_t source_byte(std::size_t visible_byte) const;
    std::size_t visible_byte(std::size_t source_byte) const;
private:
    std::string source_,visible_;
    std::vector<std::size_t> source_bytes_;
    std::vector<TextStyleRange> styles_;
    std::vector<std::string> colors_{""};
    std::vector<ParagraphIcon> icons_;
};
bool needs_native_paragraph_shaping(std::string_view source,bool formatted=true) noexcept;
std::string paragraph_color_transition(std::string_view previous,std::string_view next);
}
