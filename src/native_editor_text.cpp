#include "native_editor_text.hpp"
#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace eu4unicode {
namespace {
using Assign=EngineString*(*)(EngineString*,const char*,std::uint64_t);
using Destroy=void(*)(EngineString*);
using Move=void(*)(void*,std::uint32_t);
Assign assign=nullptr;Destroy destroy=nullptr;Move move=nullptr;
struct Rows { const std::byte* begin;const std::byte* end;const std::byte* capacity; };
std::uint16_t& word(void* widget,std::size_t offset) { return *reinterpret_cast<std::uint16_t*>(static_cast<std::byte*>(widget)+offset); }
void reset_geometry(void* widget) {
    auto base=static_cast<std::byte*>(widget);
    base[0x100]=base[0x101]=std::byte{1};
    word(widget,0xaa)=word(widget,0xac)=word(widget,0xae)=word(widget,0xb0)=0xffff;
}
}
void configure_native_editor_text(void* image) {
    const auto base=static_cast<std::byte*>(image);
    assign=reinterpret_cast<Assign>(base+0x95110);destroy=reinterpret_cast<Destroy>(base+0x95660);
    move=reinterpret_cast<Move>(base+0x1536f50);
}
NativeEditView native_edit_view(void* widget) {
    const auto base=static_cast<const std::byte*>(widget);
    const auto& source=*reinterpret_cast<const EngineString*>(base+0x30);
    if(source.size>32000) throw std::length_error("Native editor document exceeds capacity");
    const auto text=std::string_view(source.data(),static_cast<std::size_t>(source.size));
    if(!valid_utf8(text)) throw std::invalid_argument("Native editor document is not UTF-8");
    const auto rows=reinterpret_cast<const Rows*(*)(void*)>((*static_cast<void***>(widget))[0x1a0/8])(widget);
    if(!rows||rows->end<rows->begin||(rows->end-rows->begin)%40||rows->end-rows->begin>500*40)
        throw std::invalid_argument("Invalid native editor row cache");
    std::vector<EditRowInput> input;
    for(auto row=rows->begin;row!=rows->end;row+=40) {
        const auto& value=*reinterpret_cast<const EngineString*>(row);
        if(value.size>32001) throw std::length_error("Native editor row exceeds capacity");
        input.push_back({{value.data(),static_cast<std::size_t>(value.size)},row[32]!=std::byte{0}});
    }
    return {text,EditRows(text,input)};
}
EditState native_edit_state(void* widget) {
    const auto view=native_edit_view(widget);
    const auto caret=view.rows.offset({word(widget,0x56),word(widget,0x54)});
    const auto anchor=static_cast<std::byte*>(widget)[0x90]!=std::byte{0}?
        view.rows.offset({word(widget,0x94),word(widget,0x92)}):caret;
    return {std::string(view.text),align_selection(view.text,anchor,caret)};
}
void native_edit_caret(void* widget,std::size_t offset,bool clear_selection) {
    if(!move||offset>32000) throw std::invalid_argument("Invalid native editor caret");
    move(widget,static_cast<std::uint32_t>(offset));
    const auto view=native_edit_view(widget);
    const auto position=view.rows.position(offset);
    word(widget,0x56)=static_cast<std::uint16_t>(position.row);word(widget,0x54)=static_cast<std::uint16_t>(position.column);
    word(widget,0x50)=word(widget,0x54);word(widget,0x52)=0;
    if(clear_selection) {
        static_cast<std::byte*>(widget)[0x90]=std::byte{0};
        assign(reinterpret_cast<EngineString*>(static_cast<std::byte*>(widget)+0x70),"",0);
    }
    reset_geometry(widget);
}
void native_edit_align_commit(void* widget) {
    const auto view=native_edit_view(widget);
    const EditPosition current{word(widget,0x56),word(widget,0x54)};
    const auto caret=view.rows.commit_offset(current);
    const auto boundaries=grapheme_boundaries(view.text);
    const auto aligned=*std::lower_bound(boundaries.begin(),boundaries.end(),caret);
    const auto position=view.rows.position(aligned);
    if(aligned!=caret||position.row!=current.row||position.column!=current.column)
        native_edit_caret(widget,aligned);
}
void native_edit_notify(void* widget) {
    auto base=static_cast<std::byte*>(widget);
    // Update the edit geometry before notifying owners. A notification may
    // close the editor, so do not access it again after the native callbacks.
    reinterpret_cast<void(*)(void*)>((*static_cast<void***>(widget))[0x208/8])(widget);
    for(const auto slot:{0x20u,0x18u}) for(auto node=*reinterpret_cast<std::byte**>(base+8);node;) {
        auto observer=*reinterpret_cast<void**>(node);
        if(slot==0x20) reinterpret_cast<void(*)(void*)>((*static_cast<void***>(observer))[slot/8])(observer);
        else reinterpret_cast<void(*)(void*,void*)>((*static_cast<void***>(observer))[slot/8])(observer,widget);
        node=*reinterpret_cast<std::byte**>(node+0x10);
    }
}
void native_edit_restore(void* widget,const EditState& state,bool notify) {
    if(!assign||state.text.size()>32000||!valid_utf8(state.text)) throw std::invalid_argument("Invalid native editor restore state");
    const auto selection=align_selection(state.text,state.selection.anchor,state.selection.caret);
    EngineString replacement{};replacement.capacity=15;
    assign(&replacement,state.text.data(),state.text.size());
    auto base=static_cast<std::byte*>(widget);
    std::swap(replacement,*reinterpret_cast<EngineString*>(base+0x30));destroy(&replacement);
    reset_geometry(widget);native_edit_caret(widget,selection.caret);
    const auto view=native_edit_view(widget);
    const auto anchor=view.rows.position(selection.anchor);
    word(widget,0x92)=static_cast<std::uint16_t>(anchor.column);word(widget,0x94)=static_cast<std::uint16_t>(anchor.row);
    base[0x90]=static_cast<std::byte>(selection.anchor!=selection.caret);
    if(selection.anchor!=selection.caret) {
        const auto first=(std::min)(selection.anchor,selection.caret),last=(std::max)(selection.anchor,selection.caret);
        assign(reinterpret_cast<EngineString*>(base+0x70),state.text.data()+first,last-first);
    }
    word(widget,0x60)=static_cast<std::uint16_t>(view.rows.rows().size());
    *reinterpret_cast<std::uint32_t*>(base+0x64)=static_cast<std::uint32_t>(view.rows.rows().at(word(widget,0x56)).length);
    if(notify) native_edit_notify(widget);
}
}
