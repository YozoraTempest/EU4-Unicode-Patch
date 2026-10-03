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
// Return the largest complete grapheme prefix within a native byte budget.
std::size_t grapheme_prefix(std::string_view text,std::size_t byte_limit);
// Native edit-widget blacklist entries are single Latin-1 bytes, not UTF-8.
std::string filter_editor_characters(std::string_view text,std::string_view blacklist);
struct Selection { std::size_t anchor,caret; };
// Preserve selection direction while including every touched grapheme.
Selection align_selection(std::string_view text,std::size_t anchor,std::size_t caret);
struct EditResult { std::string text; std::size_t caret; };
// Validate a complete commit before replacing an outward-aligned selection.
// The returned text and caret form one state; a failed commit changes neither.
EditResult replace_selection(std::string_view text,std::size_t anchor,std::size_t caret,
    std::string_view insertion,std::size_t byte_limit);
}
