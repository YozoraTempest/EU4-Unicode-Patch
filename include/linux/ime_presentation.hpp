#pragma once
#include "linux/native.hpp"
#include <string>

namespace eu4unicode::linux_native {
struct ImePresentation {
    // Identity only: the draw hook receives the live sprite from the engine.
    // No retained native object is dereferenced through this snapshot.
    void* sprite=nullptr;
    std::string text;
    int x=0,y=0,width=0,height=0;
    int underline_begin=0,underline_end=0;
    int selection_begin=0,selection_end=0;
    int thickness=1;
};
void publish_ime_presentation(ImePresentation value);
void clear_ime_presentation() noexcept;
void prepare_ime_presentation(Hooks&);
void report_ime_presentation_counts() noexcept;
}
