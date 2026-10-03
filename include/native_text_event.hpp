#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace eu4unicode {
inline constexpr std::uint64_t native_utf8_tag=0x3154463855503445;
// Observed EU4 1.37.5 x64 queue ABI. The queue copies all 0x58 bytes.
// Type 2 uses only text[0] in the unpatched character consumer.
struct NativeTextEvent {
    std::byte prefix[16];
    char text[32];
    std::uint64_t utf8_tag;
    std::uint64_t text_kind;
    std::byte reserved_text[16];
    std::uint32_t type;
    std::uint16_t flags;
    std::uint16_t trailing;
};
static_assert(offsetof(NativeTextEvent,text)==0x10);
static_assert(offsetof(NativeTextEvent,utf8_tag)==0x30);
static_assert(offsetof(NativeTextEvent,text_kind)==0x38);
static_assert(offsetof(NativeTextEvent,type)==0x50);
static_assert(sizeof(NativeTextEvent)==0x58);
bool make_text_event(std::string_view text,NativeTextEvent& event) noexcept;
std::string_view queued_utf8(const NativeTextEvent& event) noexcept;
}
