#include "yaml_input.hpp"
#include "unicode_text.hpp"

namespace eu4unicode {
std::optional<std::string> yaml_parse_buffer(std::string_view input) {
    if(input.empty()||input.substr(0,3)=="\xef\xbb\xbf"||!valid_utf8(input)) return std::nullopt;
    std::string result("\xef\xbb\xbf");
    result.append(input.data(),input.size());
    return result;
}
}
