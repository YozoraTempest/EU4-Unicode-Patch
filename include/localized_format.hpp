#pragma once
#include <string>
#include <string_view>

namespace eu4unicode {
// Empty means the engine's original localized text should be kept.
std::string chinese_date(std::string_view day,std::string_view month,std::string_view year);
std::string chinese_month_year(std::string_view month,std::string_view year);
std::string chinese_battle_title(std::string_view prefix,std::string_view place);
}
