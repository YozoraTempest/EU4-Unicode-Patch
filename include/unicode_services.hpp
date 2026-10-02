#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace eu4unicode {
// All positions are offsets into the original UTF-8 string, including for
// supplementary scalars and clusters containing more than one scalar.
std::vector<std::size_t> grapheme_boundaries(std::string_view text);
std::vector<std::size_t> line_boundaries(std::string_view text);
std::size_t previous_grapheme(std::string_view text,std::size_t offset);
std::size_t next_grapheme(std::string_view text,std::size_t offset);
std::string search_key(std::string_view text);
std::string canonical_text(std::string_view text);
}
