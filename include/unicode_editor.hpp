#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace eu4unicode {
enum class EditKey { left,right,backspace,forward_delete };
struct EditPlan {
    std::size_t erase_begin,erase_end,caret;
};
// Native widgets can supply byte offsets inside a scalar or a grapheme.
// Erasure covers the complete containing grapheme; movement selects a boundary.
EditPlan plan_edit(std::string_view text,std::size_t caret,EditKey key);
struct EditResult { std::string text; std::size_t caret; };
// Validate a complete commit before replacing an outward-aligned selection.
// The returned text and caret form one state; a failed commit changes neither.
EditResult replace_selection(std::string_view text,std::size_t anchor,std::size_t caret,
    std::string_view insertion,std::size_t byte_limit);
}
