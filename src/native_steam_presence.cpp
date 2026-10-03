#include "native_steam_presence.hpp"
#include "unicode_text.hpp"

namespace eu4unicode {
EngineString* construct_steam_presence(EngineString* target,const EngineString* source,
    std::uintptr_t caller_rva,NativePresenceConversion convert,NativeStringAssignment assign) {
    if(caller_rva!=steam_presence_conversion_return) return convert(target,source);
    const auto value=std::string_view(source->data(),static_cast<std::size_t>(source->size));
    // Engine-owned single-byte literals still need the native conversion.
    if(!valid_utf8(value)) return convert(target,source);
    *target={};
    target->capacity=15;
    return assign(target,value.data(),static_cast<std::uint64_t>(value.size()));
}
}
