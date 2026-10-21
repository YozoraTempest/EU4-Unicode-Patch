#pragma once
#include "linux/native.hpp"
#include <string>

namespace eu4unicode::linux_native {
// AddForbiddenChars accepts native single-byte character values. Match them
// against complete Unicode scalars, never the bytes inside another scalar.
std::string filter_editor_input(std::string_view text,std::string_view forbidden);
std::size_t editor_prefix(std::string_view text,std::size_t limit,bool formatted);
void prepare_editor_mutation(Hooks&);
void report_editor_mutation_counts() noexcept;
}
