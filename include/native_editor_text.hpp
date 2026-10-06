#pragma once
#include "editor_document.hpp"
#include "engine_string.hpp"

namespace eu4unicode {
struct NativeEditView { std::string_view text;EditRows rows; };
// Guarded 1.37.5 edit-widget ABI. Row buffers remain owned by the engine.
NativeEditView native_edit_view(void* widget);
EditState native_edit_state(void* widget);
void configure_native_editor_text(void* image);
void native_edit_caret(void* widget,std::size_t offset,bool clear_selection=true);
void native_edit_align_commit(void* widget);
void native_edit_restore(void* widget,const EditState& state,bool notify=true);
void native_edit_notify(void* widget);
}
