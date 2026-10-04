#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace eu4unicode {
struct SearchSyllable {
    std::string literal;
    std::vector<std::string> readings;
    std::size_t word_end=0;
};
std::string pinyin_letters(std::string_view text);
std::string simplified_search_text(std::string_view text);
std::vector<SearchSyllable> search_syllables(std::string_view simplified);
// Replace the optional dictionary atomically. Lines use "词组: pin yin";
// repeated phrases add readings. Invalid input leaves the old table intact.
void set_pinyin_dictionary(std::string_view content);
std::uint64_t pinyin_dictionary_generation() noexcept;
}
