#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace eu4unicode {
enum class LegacyPayload { cp1252_in_utf8, raw_bytes };
enum class LegacyError { none, invalid_utf8, truncated_escape, invalid_payload,
                         invalid_unit, unpaired_surrogate, unterminated_token, token_overflow };
struct LegacyText {
    std::string text;
    std::size_t sequences=0;
    std::size_t relocated=0;
    std::size_t error_offset=0;
    LegacyError error=LegacyError::none;
};
bool contains_legacy_escape(std::string_view text) noexcept;
// Localization values carry CP1252 payload characters in UTF-8. The script
// lexer carries raw payload bytes. Only escaped units receive the old remap.
LegacyText decode_legacy_text(std::string_view text,LegacyPayload payload);
// Native text and binary lexers expose completed tokens in a fixed buffer.
// Decode atomically and clear a rejected token before the engine can consume it.
LegacyText import_legacy_script_buffer(char* buffer,std::size_t capacity,unsigned token_type);
LegacyText import_legacy_binary_buffer(char* buffer,std::size_t capacity,unsigned token_type);
const char* legacy_error_name(LegacyError error) noexcept;
}
