#include "native_keyboard.hpp"
#include "native_ime.hpp"
#include <initializer_list>
#include <windows.h>

namespace eu4unicode {
NativeKeyboardPump original_keyboard_pump=nullptr;
NativeKeyboardState native_keyboard_state=nullptr;
NativeKeyboardKey native_keyboard_key=nullptr;
bool native_speed_increase(std::uint8_t character) noexcept {
    // Keep the native '+' shortcut. Alias '=' only at the campaign speed
    // dispatch, without rewriting text events used by editors or the console.
    if(character=='+') return true;
    if(character!='='||focused_native_editor()) return false;
    for(const auto key:{VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN})
        if(GetKeyState(key)&0x8000) return false;
    return true;
}
void pump_native_keyboard(void* device) {
    original_keyboard_pump(device);
    // Match SDL's later Windows event-pump repair for Win+Space/Win+G:
    // Windows can consume the Win key-up while leaving the game in focus.
    // Use the state after queued messages have been processed, and send a
    // release through SDL so both its key state and the game queue are updated.
    int count=0;
    const auto state=native_keyboard_state(&count);
    if(!state||count<=231) return;
    if(state[227]==1&&!(GetKeyState(VK_LWIN)&0x8000)) native_keyboard_key(0,227);
    if(state[231]==1&&!(GetKeyState(VK_RWIN)&0x8000)) native_keyboard_key(0,231);
}
}
