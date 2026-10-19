#pragma once
#include <safetyhook.hpp>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>
#include <span>

namespace eu4unicode::linux_native {
// GNU C++11 CString/CUTF8String ABI in the verified Linux executable.
// Read-only view: constructors, assignment and destruction stay in the engine.
struct String {
    const char* bytes;
    std::uint64_t size;
    union { std::uint64_t capacity; char local[16]; } storage;
    std::string_view view() const noexcept { return {bytes, size}; }
};
// A borrowed native view is passed only to verified read-only engine methods.
// It is never assigned to an engine owner or passed to a native destructor.
inline String borrowed_string(std::string_view text) noexcept {
    String value{};value.bytes=text.empty()?"":text.data();value.size=text.size();
    value.storage.capacity=text.size();return value;
}
static_assert(sizeof(String)==32);
static_assert(offsetof(String,size)==8);

void log(const char* format, ...) noexcept;
bool verify_executable();
void verify_site(std::uintptr_t address);
void verify_profile();
void verify_native_instructions(std::uintptr_t address,std::span<const unsigned char> expected);

class Hooks {
public:
    SafetyHookInline& add_inline(std::uintptr_t address, void* destination);
    void add_mid(std::uintptr_t address, safetyhook::MidHookFn destination);
    void enable();
    void clear() noexcept;
private:
    std::vector<std::unique_ptr<SafetyHookInline>> inline_;
    std::vector<SafetyHookMid> mid_;
};
void prepare_localization(Hooks&);
void prepare_script_bom(Hooks&);
void prepare_localized_format(Hooks&);
void report_localized_format_counts() noexcept;
void report_script_counts() noexcept;
void prepare_bitmap_fonts(Hooks&);
void prepare_text_layout(Hooks&);
void report_layout_counts() noexcept;
void prepare_map_names(Hooks&);
void report_map_counts() noexcept;
void prepare_mod_registry(Hooks&);
void prepare_editor_input(Hooks&);
void prepare_multiplayer_name(Hooks&);
void prepare_search(Hooks&);
void prepare_ime(Hooks&);
void configure_ime();
void report_ime_counts() noexcept;
void prepare_name_presence(Hooks&);
void report_name_presence_counts() noexcept;
void report_input_counts() noexcept;
void report_text_counts() noexcept;
}
