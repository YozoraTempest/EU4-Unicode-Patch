#include "native_editor_presentation.hpp"
#include "native_editor_text.hpp"
#include "native_ime.hpp"
#include <algorithm>
#include <cstring>

namespace eu4unicode {
namespace {
std::byte* game_image=nullptr;
thread_local void* active_outer=nullptr;
thread_local const CompositionPreview* active_preview=nullptr;
}
void configure_editor_presentation(void* image) { game_image=static_cast<std::byte*>(image); }
const CompositionPreview* editor_preedit_preview(void* outer) noexcept { return outer==active_outer?active_preview:nullptr; }
EditorPresentation::EditorPresentation(void* outer) noexcept {
    try {
        if(!game_image||active_outer) return;
        const auto base=static_cast<std::byte*>(outer);
        if(base[0x260]!=std::byte{1}||base[0x262]!=std::byte{0}||*reinterpret_cast<std::uint64_t*>(base+0x2d0)) return;
        const auto manager=*reinterpret_cast<std::byte**>(game_image+0x23494f0);
        if(!manager||*reinterpret_cast<void**>(manager+0x210)!=base+0x1d0) return;
        const auto composition=native_composition();
        if(!composition.active||composition.value.text.empty()) return;
        auto widget=base+0xc8;
        preview_=composition_preview(native_edit_state(widget),composition.value);
        EngineString display{};display.capacity=15;
        reinterpret_cast<void(*)(EngineString*,const char*,std::uint64_t)>(game_image+0x95110)(
            &display,preview_->text.data(),preview_->text.size());
        std::memcpy(position_.data(),widget+0x50,position_.size());
        std::memcpy(selection_.data(),widget+0x90,selection_.size());
        std::memcpy(geometry_.data(),widget+0xa2,geometry_.size());
        std::swap(display,*reinterpret_cast<EngineString*>(widget+0x30));source_=display;outer_=outer;
        widget[0x100]=widget[0x101]=std::byte{1};
        native_edit_caret(widget,preview_->caret,false);widget[0x90]=std::byte{0};
        active_outer=outer;active_preview=&*preview_;
    } catch(...) {
        if(outer_) {
            auto widget=static_cast<std::byte*>(outer_)+0xc8;
            std::swap(source_,*reinterpret_cast<EngineString*>(widget+0x30));
            reinterpret_cast<void(*)(EngineString*)>(game_image+0x95660)(&source_);
            std::memcpy(widget+0x50,position_.data(),position_.size());
            std::memcpy(widget+0x90,selection_.data(),selection_.size());
            std::memcpy(widget+0xa2,geometry_.data(),geometry_.size());
            widget[0x100]=widget[0x101]=std::byte{1};outer_=nullptr;
        }
        preview_.reset();
    }
}
EditorPresentation::~EditorPresentation() {
    if(!outer_) return;
    active_outer=nullptr;active_preview=nullptr;
    auto widget=static_cast<std::byte*>(outer_)+0xc8;
    std::swap(source_,*reinterpret_cast<EngineString*>(widget+0x30));
    reinterpret_cast<void(*)(EngineString*)>(game_image+0x95660)(&source_);
    std::memcpy(widget+0x50,position_.data(),position_.size());
    std::memcpy(widget+0x90,selection_.data(),selection_.size());
    std::memcpy(widget+0xa2,geometry_.data(),geometry_.size());
    // The cached rows may contain the preview. They must be rebuilt from the
    // restored source before any later input, saving or game notification.
    widget[0x100]=widget[0x101]=std::byte{1};
}
}
