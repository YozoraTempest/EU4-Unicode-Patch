#pragma once
#include <safetyhook.hpp>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace eu4unicode::linux_native {
// GNU C++11 CString/CUTF8String ABI in the verified Linux executable.
// Read-only view: constructors, assignment and destruction stay in the engine.
struct String {
    const char* bytes;
    std::uint64_t size;
    union { std::uint64_t capacity; char local[16]; } storage;
    std::string_view view() const noexcept { return {bytes, size}; }
};
static_assert(sizeof(String)==32);
static_assert(offsetof(String,size)==8);

void log(const char* format, ...) noexcept;
bool verify_executable();
void verify_site(std::uintptr_t address);
void verify_profile();

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
void prepare_bitmap_fonts(Hooks&);
void prepare_mod_registry(Hooks&);
void prepare_editor_input(Hooks&);
void prepare_multiplayer_name(Hooks&);
void prepare_search(Hooks&);
void prepare_ime(Hooks&);
void report_input_counts() noexcept;
void report_text_counts() noexcept;
}
