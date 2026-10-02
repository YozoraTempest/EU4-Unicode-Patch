#include "unicode_text.hpp"
#include <utf8.h>
#include <iterator>

namespace eu4unicode {
std::string encode(std::uint32_t scalar) {
    std::string result;
    utf8::append(scalar,std::back_inserter(result));
    return result;
}
Scalar decode(std::string_view text) noexcept {
    if (text.empty()) return {0, 0, false};
    auto cursor = text.begin();
    try {
        const auto cp = utf8::next(cursor, text.end());
        return {cp, static_cast<std::size_t>(cursor - text.begin()), true};
    } catch (...) {
        return {0xfffd, 1, false};
    }
}
bool valid_utf8(std::string_view text) noexcept {
    return utf8::is_valid(text.begin(), text.end());
}
std::size_t scalar_prefix(std::string_view text, std::size_t limit) noexcept {
    if (text.size() <= limit) return text.size();
    std::size_t end = limit;
    // A UTF-8 scalar occupies at most four bytes. Find the possible leading
    // byte, then keep it only if its complete encoding fits in the buffer.
    while (end && limit - end < 3 &&
           (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) --end;
    const auto scalar = decode(text.substr(end));
    return scalar.valid && end + scalar.bytes > limit ? end : limit;
}
std::uint32_t bitmap_slot(std::uint32_t cp) noexcept {
    // Higher code points use the sparse font registry, not inline engine slots.
    return cp;
}
}
