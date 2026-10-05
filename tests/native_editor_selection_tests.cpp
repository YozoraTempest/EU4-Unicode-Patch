#include "native_editor_selection.hpp"
#include <array>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {
using namespace eu4unicode;
void require(bool value) { if(!value) throw std::runtime_error("Native selection sprite contract failed"); }
struct Sprite {
    void** methods;
    NativeSelectionRect rect{};
    bool shown=false;
    void* parent=nullptr;
};
void show(void* sprite) { static_cast<Sprite*>(sprite)->shown=true; }
void hide(void* sprite) { static_cast<Sprite*>(sprite)->shown=false; }
void reset(void* sprite) { static_cast<Sprite*>(sprite)->parent=nullptr; }
void parent(void* sprite,void* value) { static_cast<Sprite*>(sprite)->parent=value; }
void position(void* sprite,const int* value) { auto& box=static_cast<Sprite*>(sprite)->rect;box.x=value[0];box.y=value[1]; }
void size(void* sprite,std::uint64_t value) {
    auto& box=static_cast<Sprite*>(sprite)->rect;box.width=static_cast<int>(value);box.height=static_cast<int>(value>>32);
}
std::array<void*,64> methods{};
std::vector<std::unique_ptr<Sprite>> created;
int destroyed=0,text_destroyed=0;
void* manager=nullptr;
void* context=nullptr;
void* create(void* received_manager,const EngineString* name,void* received_context,unsigned char flags,EngineString* output) {
    require(received_manager==manager&&received_context==context&&flags==7);
    require(std::string_view(name->data(),name->size)=="gfx_transparency_white");
    require(output->size==0&&output->capacity==15);
    created.push_back(std::make_unique<Sprite>(Sprite{methods.data()}));return created.back().get();
}
void destroy(void* received_context,void* sprite) {
    require(received_context==context);
    for(auto& item:created) if(item.get()==sprite) { item.reset();++destroyed;return; }
    require(false);
}
void destroy_text(EngineString*) { ++text_destroyed; }
}
void verify_native_editor_selections() {
    using namespace eu4unicode;
    methods[0x60/8]=reinterpret_cast<void*>(show);methods[0x68/8]=reinterpret_cast<void*>(hide);
    methods[0xe0/8]=reinterpret_cast<void*>(reset);methods[0xc8/8]=reinterpret_cast<void*>(parent);
    methods[0x168/8]=reinterpret_cast<void*>(position);methods[0x1d0/8]=reinterpret_cast<void*>(size);
    int owner=0,parent_one=0,parent_two=0,manager_value=0,context_value=0;
    manager=&manager_value;context=&context_value;
    Sprite original{methods.data()};
    NativeEditorSelections selections(create,destroy,destroy_text);
    selections.capture(&original,manager,context,7);
    const std::vector<NativeSelectionRect> boxes{{120,30,18,11},{190,30,27,11},{250,30,8,11}};
    selections.update(&owner,&original,&parent_one,boxes);
    require(original.shown&&created.size()==2&&text_destroyed==2);
    require(original.rect.x==120&&original.rect.width==18);
    require(created[0]->rect.x==190&&created[0]->rect.width==27&&created[0]->shown&&created[0]->parent==&parent_one);
    selections.setup(&owner,&parent_two);require(created[1]->parent==&parent_two);
    selections.update(&owner,&original,&parent_two,{boxes[0]});
    require(original.shown&&!created[0]->shown&&!created[1]->shown);
    selections.update(&owner,&original,&parent_two,boxes);require(created.size()==2);
    selections.hide(&owner);require(!created[0]->shown&&!created[1]->shown);
    selections.update(&owner,&original,&parent_two,{});require(!original.shown);
    selections.release(&owner,&original);require(destroyed==2);
    selections.hide(&owner);selections.release(&owner,&original);require(destroyed==2);
}
