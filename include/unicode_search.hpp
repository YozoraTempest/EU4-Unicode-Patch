#pragma once
#include <string_view>

namespace eu4unicode {
// Display-name matching only. Original text and internal identifiers are never
// changed. Preserve the observed game's Latin accent-insensitive search while
// also accepting full Unicode casefold and compatibility-width matches.
bool country_search_contains(std::string_view name,std::string_view query);
}
