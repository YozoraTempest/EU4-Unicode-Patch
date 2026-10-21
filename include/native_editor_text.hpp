#pragma once
#include "editor_document.hpp"
#include "engine_string.hpp"

namespace eu4unicode {
struct NativeEditView { std::string_view text;EditRows rows; };
// Guarded 1.37.5 edit-widget ABI. Row buffers remain owned by the engine.
NativeEditView native_edit_view(void* widget);
EditState native_edit_state(void* widget);
void configure_native_editor_text(void* image);
struct NativeEditorTextApi {
    EngineString*(*assign)(EngineString*,const char*,std::uint64_t);
    void(*destroy)(EngineString*);
    void(*move)(void*,std::uint32_t);
};
void configure_native_editor_text(NativeEditorTextApi api);
void native_edit_select_all(void* widget);
using NativeSpaceRow=EngineString*(*)(EngineString*,const EngineString*);
extern NativeSpaceRow original_editor_space_row;
EngineString* construct_editor_space_row(EngineString* row,const EngineString* source);
void native_edit_caret(void* widget,std::size_t offset,bool clear_selection=true);
void native_edit_align_commit(void* widget);
void native_edit_restore(void* widget,const EditState& state,bool notify=true);
void native_edit_notify(void* widget);
}
