#pragma once
#include "shaped_paragraph.hpp"
#include "scalar_glyph.hpp"
#include "font_draw_batches.hpp"

namespace eu4unicode {
struct NativeLineAdvance {
    struct Icon { std::uint32_t id;int padding; };
    std::vector<Icon> icons;
    int finish;
    float pixels;
};
NativeLineAdvance plan_paragraph_line(const ShapedParagraph& paragraph,std::size_t line,float scale);
struct ParagraphTransport {
    std::string text;
    std::vector<ScalarGlyph> glyphs;
    // Byte positions distinguish generated tokens from literal PUA characters
    // retained as native inline glyph commands.
    std::vector<std::size_t> tokens;
};
// Tokens initially start at U+F0000. The owning atlas assigns an invocation-
// local range after validating the complete plan and reserving cache space.
ParagraphTransport plan_paragraph_transport(const ShapedParagraph& paragraph,
                                            FontPageSize page,float scale);
std::uint32_t paragraph_token_start(const ParagraphTransport& plan,std::uint32_t first,
                                    std::uint32_t end=0xffffe);
void assign_paragraph_tokens(ParagraphTransport& plan,std::uint32_t first);
}
