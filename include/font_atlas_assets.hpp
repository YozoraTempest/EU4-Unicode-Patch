#pragma once
#include "unicode_layout.hpp"
#include <set>

namespace eu4unicode {
void write_font_atlas(const std::set<std::uint32_t>& scalars,const std::filesystem::path& destination,
                      int size,int width,int minimum_height,
                      const std::shared_ptr<const TextFonts>& fonts={},bool require_font_files=false);
// Called from the native font loader, after Windows has released its DLL loader lock.
// Each font is generated once per process from the current system font collection.
void ensure_player_font_atlas(const std::filesystem::path& game_directory,std::string_view font_path);
}
