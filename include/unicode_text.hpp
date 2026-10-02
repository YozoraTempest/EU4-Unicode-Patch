#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace eu4unicode {
struct Scalar {
    std::uint32_t value;
    std::size_t bytes;
    bool valid;
};
Scalar decode(std::string_view text) noexcept;
bool valid_utf8(std::string_view text) noexcept;
// This is a glyph address in the installed BMFont fixture, not a text encoding.
std::uint32_t bitmap_slot(std::uint32_t scalar) noexcept;
}
