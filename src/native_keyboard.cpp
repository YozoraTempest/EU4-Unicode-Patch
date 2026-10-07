#include "native_keyboard.hpp"
#include <windows.h>

namespace eu4unicode {
NativeKeyboardPump original_keyboard_pump=nullptr;
NativeKeyboardState native_keyboard_state=nullptr;
NativeKeyboardKey native_keyboard_key=nullptr;
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
