#pragma once
#include <cstddef>
#include <cstdint>

namespace eu4unicode {
// String layout of the verified EU4 1.37.5 x64 executable. Allocations and
// destruction remain with the engine; this type only exposes its ABI.
struct EngineString {
    union { char inline_bytes[16]; const char* pointer; } storage;
    std::uint64_t size;
    std::uint64_t capacity;
    const char* data() const { return capacity<16?storage.inline_bytes:storage.pointer; }
};
static_assert(sizeof(EngineString)==32);
static_assert(offsetof(EngineString,size)==16);
static_assert(offsetof(EngineString,capacity)==24);
}
