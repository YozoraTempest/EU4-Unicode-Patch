#include "native_editor_selection.hpp"
#include <cstring>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace eu4unicode {
namespace {
template<class Fn> Fn method(void* sprite,std::size_t offset) {
    return reinterpret_cast<Fn>((*static_cast<void***>(sprite))[offset/sizeof(void*)]);
}
void hide_sprite(void* sprite) { method<void(*)(void*)>(sprite,0x68)(sprite); }
void setup_sprite(void* sprite,void* value) {
    method<void(*)(void*)>(sprite,0xe0)(sprite);
    method<void(*)(void*,void*)>(sprite,0xc8)(sprite,value);
}
void paint_sprite(void* sprite,const NativeSelectionRect& box) {
    const auto size=static_cast<std::uint64_t>(static_cast<std::uint32_t>(box.width))|
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(box.height))<<32);
    method<void(*)(void*,std::uint64_t)>(sprite,0x1d0)(sprite,size);
    const int point[]{box.x,box.y};
    method<void(*)(void*,const int*)>(sprite,0x168)(sprite,point);
    method<void(*)(void*)>(sprite,0x60)(sprite);
}
}
struct NativeEditorSelections::Impl {
    struct Resource { void* manager;void* context;unsigned char flags; };
    struct Group { Resource resource;std::vector<void*> sprites; };
    NativeSpriteFactory create;
    NativeSpriteDestroy destroy;
    NativeStringDestroy destroy_text;
    std::unordered_map<void*,Resource> resources;
    std::unordered_map<void*,Group> groups;
};
NativeEditorSelections::NativeEditorSelections(NativeSpriteFactory create,NativeSpriteDestroy destroy,NativeStringDestroy destroy_text):
    impl_(new Impl{create,destroy,destroy_text,{},{}}) {}
// Native GUI objects are released by their editor destructor, while its context
// is still alive. DLL shutdown must not call back into a destroyed GUI manager.
NativeEditorSelections::~NativeEditorSelections() { delete impl_; }
void NativeEditorSelections::capture(void* prototype,void* manager,void* context,unsigned char flags) {
    if(prototype) impl_->resources.insert_or_assign(prototype,Impl::Resource{manager,context,flags});
}
void NativeEditorSelections::update(void* owner,void* prototype,void* setup,const std::vector<NativeSelectionRect>& rectangles) {
    if(!prototype) { hide(owner);return; }
    const auto found=impl_->resources.find(prototype);
    if(found==impl_->resources.end()) throw std::logic_error("Native selection resource was not captured");
    if(rectangles.size()>4096) throw std::length_error("Native selection exceeds rectangle capacity");
    for(const auto& box:rectangles) if(box.width<=0||box.height<=0)
        throw std::invalid_argument("Invalid native selection rectangle");
    auto& group=impl_->groups.try_emplace(owner,Impl::Group{found->second,{}}).first->second;
    const auto extra=rectangles.empty()?0:rectangles.size()-1;
    while(group.sprites.size()<extra) {
        group.sprites.reserve(extra);
        EngineString name{},output{};
        constexpr char resource[]="gfx_transparency_white";
        name.storage.pointer=resource;name.size=sizeof(resource)-1;name.capacity=sizeof(resource);
        output.capacity=15;
        auto sprite=impl_->create(group.resource.manager,&name,group.resource.context,group.resource.flags,&output);
        impl_->destroy_text(&output);
        if(!sprite) throw std::runtime_error("Native selection sprite creation failed");
        group.sprites.push_back(sprite);hide_sprite(sprite);
        if(setup) setup_sprite(sprite,setup);
    }
    if(rectangles.empty()) hide_sprite(prototype);else paint_sprite(prototype,rectangles.front());
    for(std::size_t index=0;index<group.sprites.size();++index) {
        if(index<extra) paint_sprite(group.sprites[index],rectangles[index+1]);
        else hide_sprite(group.sprites[index]);
    }
}
void NativeEditorSelections::setup(void* owner,void* value) {
    const auto found=impl_->groups.find(owner);
    if(found!=impl_->groups.end()) for(auto sprite:found->second.sprites) setup_sprite(sprite,value);
}
void NativeEditorSelections::hide(void* owner) {
    const auto found=impl_->groups.find(owner);
    if(found!=impl_->groups.end()) for(auto sprite:found->second.sprites) hide_sprite(sprite);
}
void NativeEditorSelections::release(void* owner,void* prototype) {
    const auto found=impl_->groups.find(owner);
    if(found!=impl_->groups.end()) {
        for(auto sprite:found->second.sprites) impl_->destroy(found->second.resource.context,sprite);
        impl_->groups.erase(found);
    }
    impl_->resources.erase(prototype);
}
}
