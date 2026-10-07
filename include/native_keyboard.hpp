#pragma once
#include <cstdint>

namespace eu4unicode {
using NativeKeyboardPump=void(*)(void*);
using NativeKeyboardState=const std::uint8_t*(*)(int*);
using NativeKeyboardKey=int(*)(std::uint8_t,int);
extern NativeKeyboardPump original_keyboard_pump;
extern NativeKeyboardState native_keyboard_state;
extern NativeKeyboardKey native_keyboard_key;
void pump_native_keyboard(void* device);
}
