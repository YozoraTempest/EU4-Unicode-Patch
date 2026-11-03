#include "native_color_strip.hpp"
#include "formatted_text.hpp"

namespace eu4unicode {
ColorStringAssign native_color_assign=nullptr;
void(*native_color_log)(const char*)=nullptr;

EngineString* construct_colorless_text(const EngineString* source,EngineString* target) noexcept {
    *target={};
    target->capacity=15;
    try {
        native_color_assign(target,source->data(),source->size);
        target->size=strip_native_colors(const_cast<char*>(target->data()),
            static_cast<std::size_t>(target->size));
    } catch(...) {
        target->size=0;
        const_cast<char*>(target->data())[0]='\0';
        if(native_color_log) native_color_log("Unicode color stripping failed; text excluded.");
    }
    return target;
}
}
