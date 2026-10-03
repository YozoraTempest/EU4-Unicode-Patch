#pragma once
#include "glyph_registry.hpp"
#include "unicode_layout.hpp"

namespace eu4unicode {
struct ScalarGlyph {
    NativeGlyph metrics{};
    std::vector<std::uint8_t> alpha;
};
// Isolated-scalar bitmap bridge for the native EU4 font iterator. Contextual
// scripts continue to require the complete shaped-run renderer.
ScalarGlyph rasterize_scalar(std::uint32_t scalar,int size,
                            std::shared_ptr<const TextFonts> fonts={});
}
