#pragma once
#include <string_view>
#include <string>
#include <utility>

namespace eu4unicode {
// Display-name matching only. Original text and internal identifiers are never
// changed. Preserve the observed game's Latin accent-insensitive search while
// also accepting full Unicode casefold and compatibility-width matches.
bool country_search_contains(std::string_view name,std::string_view query);
bool display_search_contains(std::string_view name,std::string_view query);
std::string latin_search_key(std::string_view text);
// A negative result retains the native province finder's fuzzy distance.
int display_search_distance(std::string_view name,std::string_view query);
std::pair<std::string,std::string> display_search_latin_keys(std::string_view name,std::string_view query);
}
