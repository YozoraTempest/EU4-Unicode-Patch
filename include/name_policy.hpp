#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace eu4unicode {
enum class NameOrder { given_first, surname_first };
enum class NameSeparator { automatic, none, space, middle_dot };
struct NamePolicy {
    NameOrder order=NameOrder::given_first;
    NameSeparator separator=NameSeparator::space;
};
bool culture_name_policy(std::string_view culture,NamePolicy& policy) noexcept;
bool parse_name_policy(std::string_view value,NamePolicy& policy) noexcept;
std::string format_person_name(std::string_view given,std::string_view family,NamePolicy policy);

bool surname_first(std::string_view suffix,std::size_t& surname_offset);
}
