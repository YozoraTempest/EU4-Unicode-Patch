#pragma once
#include "unicode_search.hpp"
#include <filesystem>
#include <string_view>

namespace eu4unicode {
SearchOptions parse_search_config(std::string_view content);
// The caller reports errors; a rejected dictionary retains builtin readings.
bool load_search_resources(const std::filesystem::path& directory);
}
