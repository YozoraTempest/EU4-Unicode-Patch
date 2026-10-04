#pragma once
#include "unicode_pinyin.hpp"

namespace eu4unicode {
// Complete pinyin only, one insertion, deletion, substitution or adjacent
// transposition. Phrase alternatives remain separate until their word ends.
bool pinyin_typo_matches(const std::vector<SearchSyllable>& units,std::string_view query);
std::vector<std::string> pinyin_fuzzy_forms(std::string_view syllable);
}
