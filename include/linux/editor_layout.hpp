#pragma once
#include "linux/native.hpp"
#include "unicode_layout.hpp"
#include <optional>

namespace eu4unicode::linux_native {
// Geometry is active only inside a verified, non-password CEditBox call.
// The scope borrows the buffer for that call and never retains its row/font.
class EditorLayoutScope {
public:
    explicit EditorLayoutScope(void* editor) noexcept;
    ~EditorLayoutScope();
    EditorLayoutScope(const EditorLayoutScope&)=delete;
    EditorLayoutScope& operator=(const EditorLayoutScope&)=delete;
private:
    void* previous_;
};
bool editor_layout_hit(void* buffer,const String* source,const int* point);
std::optional<float> editor_layout_x(void* buffer,std::string_view text,bool anchor=false);
std::optional<std::vector<SelectionRegion>> editor_layout_selection(void* buffer,std::string_view row);
// Affinity belongs to the document/cursor, survives focus changes and is
// removed when the owning native editor is destroyed.
void forget_editor_layout(void* editor) noexcept;
void prepare_editor_layout(Hooks&);
void report_editor_layout_counts() noexcept;
}
