#pragma once
#include <windows.h>

namespace eu4unicode {
struct ImeRect { int x,y,w,h; };
using NativeImeMessage=int(*)(HWND,UINT,WPARAM,LPARAM*,void*);
using NativeImeRect=void(*)(void*,const ImeRect*);
extern NativeImeMessage original_ime_message;
extern NativeImeRect original_ime_rect;
int show_native_ime_candidates(HWND window,UINT message,WPARAM parameter,LPARAM* flags,void* video);
void position_native_ime_candidates(void* device,const ImeRect* rect);
}
