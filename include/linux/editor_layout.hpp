#pragma once
#include "linux/native.hpp"
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
void forget_editor_layout(void* editor) noexcept;
void prepare_editor_layout(Hooks&);
void report_editor_layout_counts() noexcept;
}
