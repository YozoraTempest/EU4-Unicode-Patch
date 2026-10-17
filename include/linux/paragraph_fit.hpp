#pragma once
#include "linux/font_pages.hpp"
#include "formatted_text.hpp"
#include <optional>

namespace eu4unicode::linux_native {
struct ParagraphFit {
    std::string text;
    int height;
    std::size_t lines;
    bool clipped,ellipsis;
};
// CString::GetSubstring takes [begin,end), not a start and a byte count.
void align_font_substring(const FormattedText& text,std::size_t& begin,std::size_t& end);
// Returned text always contains logical source bytes, never draw tokens.
// Native fitting reports the attempted next line in its height on overflow.
std::optional<ParagraphFit> fit_paragraph(void* font,std::string_view source,
    int width,int height,const unsigned* padding,bool formatted,bool break_lines,bool ellipsis);
}
