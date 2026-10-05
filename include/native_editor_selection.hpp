#pragma once
#include "engine_string.hpp"
#include <cstdint>
#include <vector>

namespace eu4unicode {
struct NativeSelectionRect { int x,y,width,height; };
using NativeSpriteFactory=void*(*)(void*,const EngineString*,void*,unsigned char,EngineString*);
using NativeSpriteDestroy=void(*)(void*,void*);
using NativeStringDestroy=void(*)(EngineString*);
// Reuse the editor's native selection resource and rendering context. Extra
// rectangles follow the same parent setup and lifetime as the original sprite.
class NativeEditorSelections {
public:
    NativeEditorSelections(NativeSpriteFactory create,NativeSpriteDestroy destroy,NativeStringDestroy destroy_text);
    ~NativeEditorSelections();
    NativeEditorSelections(const NativeEditorSelections&)=delete;
    NativeEditorSelections& operator=(const NativeEditorSelections&)=delete;
    void capture(void* prototype,void* manager,void* context,unsigned char flags);
    void update(void* owner,void* prototype,void* setup,const std::vector<NativeSelectionRect>& rectangles);
    void setup(void* owner,void* value);
    void hide(void* owner);
    void release(void* owner,void* prototype);
private:
    struct Impl;
    Impl* impl_;
};
}
