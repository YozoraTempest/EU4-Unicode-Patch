#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "unicode_text.hpp"

namespace eu4unicode {
enum class TextUnitKind { glyph, color, icon, flag };
struct TextUnit {
    TextUnitKind kind;
    std::uint32_t scalar;
    std::size_t begin,end;
};
// Native compiled literals can contain the same single-byte glyphs supported
// by the existing engine adapter. Imported localization remains strict UTF-8.
TextUnit native_text_unit(std::string_view text,std::size_t offset,bool formatted=true);
std::size_t native_scalar_start(std::string_view text,std::size_t offset) noexcept;
std::size_t native_scalar_next(std::string_view text,std::size_t offset) noexcept;
Scalar native_measure_scalar(std::string_view text) noexcept;

class FormattedText {
public:
    explicit FormattedText(std::string_view text,bool formatted=true);
    std::size_t prefix(std::size_t limit) const noexcept;
    bool line_before(std::size_t offset) const noexcept;
    const std::string& visible_text() const noexcept { return visible_; }
    const std::vector<std::size_t>& prefixes() const noexcept { return prefixes_; }
    std::size_t memory_size() const noexcept;
private:
    std::string visible_;
    std::vector<std::size_t> prefixes_,lines_;
};
bool layout_substring_caller(std::uintptr_t caller) noexcept;
}
