#pragma once
#include <windows.h>
#include "editor_document.hpp"

namespace eu4unicode {
struct ImeRect { int x,y,w,h; };
using NativeImeMessage=int(*)(HWND,UINT,WPARAM,LPARAM*,void*);
using NativeImeRect=void(*)(void*,const ImeRect*);
extern NativeImeMessage original_ime_message;
extern NativeImeRect original_ime_rect;
using NativeTextInputAction=void(*)();
extern NativeTextInputAction start_native_text_input,stop_native_text_input;
using NativeEditorBlur=void(*)(void*);
extern NativeEditorBlur native_editor_blur;
void focus_native_editor(void* owner);
void blur_native_editor(void* owner);
void* focused_native_editor() noexcept;
bool exit_native_editor();
int show_native_ime_candidates(HWND window,UINT message,WPARAM parameter,LPARAM* flags,void* video);
void position_native_ime_candidates(void* device,const ImeRect* rect);
struct NativeComposition { HWND window=nullptr;CompositionText value;bool active=false; };
NativeComposition native_composition();
bool native_ime_owns_edit_keys();
void clear_native_composition() noexcept;
}
