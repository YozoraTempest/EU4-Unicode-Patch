#pragma once
#include "linux/font_pages.hpp"

namespace eu4unicode::linux_native {
// Each invocation masks its parent unless the input is a copied substring of
// that parent's prepared draw string. Tokens never enter native text owners.
class ParagraphScope {
public:
    ParagraphScope(void* font,const String* source);
    ~ParagraphScope();
    ParagraphScope(const ParagraphScope&)=delete;
    ParagraphScope& operator=(const ParagraphScope&)=delete;
    const String* prepare(const String* source,float pixels,bool wrap,bool formatted,const char* renderer) noexcept;
    bool active() const noexcept {return bool(geometry_);}
private:
    friend NativeGlyph* find_paragraph_glyph(void* const*,std::uint32_t) noexcept;
    friend bool paragraph_transport_source(void*,std::string_view) noexcept;
    ParagraphScope* previous_;
    void* const* table_;
    std::shared_ptr<const FontParagraph> geometry_;
    std::string normalized_;
    std::string_view text_;
    String native_{};
};
NativeGlyph* find_paragraph_glyph(void* const* table,std::uint32_t token) noexcept;
bool paragraph_transport_source(void* font,std::string_view text) noexcept;
void report_paragraph_failure(const char* operation,const char* message) noexcept;
// Owned visual projection only; the identity is never dereferenced here.
void publish_editor_view(void* font,std::string text,ParagraphViewport viewport);
void clear_editor_view() noexcept;
}
