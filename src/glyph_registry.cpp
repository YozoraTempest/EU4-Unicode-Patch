#include "glyph_registry.hpp"
#include <mutex>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace eu4unicode {
namespace {
using Glyphs=std::unordered_map<std::uint32_t,std::unique_ptr<NativeGlyph>>;
std::unordered_map<const void*,Glyphs> fonts;
std::shared_mutex font_mutex;
const void* identity(void* const* table) noexcept { return table[0x41]?table[0x41]:table; }
}
bool bind_unicode_font(void* const* table) noexcept {
    if(!table||!table[0x41]) return false;
    try {
        std::unique_lock<std::shared_mutex> lock(font_mutex);
        const auto anchor=identity(table);
        if(anchor==table||fonts.find(table)==fonts.end()) return true;
        auto& bound=fonts[anchor];
        // Creating bound can rehash fonts; obtain pending afterwards.
        auto& pending=fonts.at(table);
        bound.merge(pending);
        if(!pending.empty()) return false;
        fonts.erase(table);
        return true;
    } catch(...) { return false; }
}
NativeGlyph* allocate_unicode_glyph(void* const* table,std::uint32_t scalar) noexcept {
    if(!table||scalar<=0xff||scalar>0x10ffff||(scalar>=0xd800&&scalar<=0xdfff)) return nullptr;
    try {
        std::unique_lock<std::shared_mutex> lock(font_mutex);
        auto record=std::make_unique<NativeGlyph>();
        auto pointer=record.get();
        return fonts[identity(table)].emplace(scalar,std::move(record)).second?pointer:nullptr;
    } catch(...) { return nullptr; }
}
void* find_unicode_glyph(void* const* table,std::uint32_t scalar) noexcept {
    if(!table||scalar<=0xff||scalar>0x10ffff) return nullptr;
    try {
        std::shared_lock<std::shared_mutex> lock(font_mutex);
        const auto font=fonts.find(identity(table));
        if(font==fonts.end()) return nullptr;
        const auto glyph=font->second.find(scalar);
        return glyph==font->second.end()?nullptr:glyph->second.get();
    } catch(...) { return nullptr; }
}
}
