#pragma once
#include <string_view>

namespace eu4unicode {
// Only known text atlases are redirected; custom symbol/icon fonts retain
// their original resources. Colors and effects belong to the native font.
std::string_view player_font_path(std::string_view native_path) noexcept;
}
