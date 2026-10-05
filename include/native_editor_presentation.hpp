#pragma once
#include "editor_document.hpp"
#include "engine_string.hpp"
#include <array>
#include <optional>

namespace eu4unicode {
void configure_editor_presentation(void* image);
const CompositionPreview* editor_preedit_preview(void* outer) noexcept;
// Exchange only presentation text during a draw. Engine-owned source bytes,
// selection and undo history are restored before returning to the caller.
class EditorPresentation {
public:
    explicit EditorPresentation(void* outer) noexcept;
    ~EditorPresentation();
    EditorPresentation(const EditorPresentation&)=delete;
    EditorPresentation& operator=(const EditorPresentation&)=delete;
private:
    void* outer_=nullptr;
    EngineString source_{};
    std::array<std::byte,32> position_{};
    std::array<std::byte,8> selection_{};
    std::array<std::byte,16> geometry_{};
    std::optional<CompositionPreview> preview_;
};
}
