#pragma once
#include "engine_string.hpp"
#include "glyph_registry.hpp"
#include "shaped_paragraph.hpp"

namespace eu4unicode {
struct NativeParagraph {
    std::shared_ptr<const ShapedParagraph> layout;
    std::string draw_text;
    std::uint32_t first_token;
    std::vector<NativeGlyph> records;
    std::vector<NativeGlyph*> glyphs;
};
// Transport tokens are confined to a native draw invocation. They never enter
// localization, editable text, saves, search or the Unicode scalar registry.
const EngineString* begin_native_paragraph(void* font,const EngineString* source,const int* box,int inset,bool formatted=true) noexcept;
const EngineString* begin_native_button_paragraph(void* font,const EngineString* source,int width,const int* margin,bool formatted) noexcept;
const EngineString* begin_native_popup_paragraph(void* font,const EngineString* source,int width) noexcept;
void end_native_paragraph() noexcept;
NativeGlyph* find_paragraph_glyph(void* const* table,std::uint32_t token) noexcept;
using NativeTextWidth=int(*)(void*,const char*,int,bool);
extern NativeTextWidth original_text_width;
int measure_paragraph_text(void* font,const char* source,int length,bool formatted);
using NativeTextHeight=int(*)(void*,const EngineString*,int,int,const int*,bool);
extern NativeTextHeight original_text_height;
int measure_paragraph_height(void* font,const EngineString* source,int width,int height,const int* margin,bool formatted);
void configure_paragraph_log(void(*log)(const char*)) noexcept;
using NativeParagraphColor=bool(*)(void*,unsigned char,std::uint32_t*);
extern NativeParagraphColor native_paragraph_color;
struct NativeLineAdvance {
    struct Icon { std::uint32_t id;int padding; };
    std::vector<Icon> icons;
    int finish;
    float pixels;
};
NativeLineAdvance plan_paragraph_line(const ShapedParagraph& paragraph,std::size_t line,float scale);
}
