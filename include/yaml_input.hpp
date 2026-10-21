#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace eu4unicode {
// Supply the signature required by the native parser only for valid UTF-8
// that lacks it. An absent result means the original buffer must be used.
std::optional<std::string> yaml_parse_buffer(std::string_view input);
}
