#include "native_editor_text.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
using namespace eu4unicode;
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void destroy(EngineString* value) {if(value->capacity>=16) delete[] value->storage.pointer;*value={};value->capacity=15;}
EngineString* assign(EngineString* value,const char* bytes,std::uint64_t size) {
    const std::string copy(bytes,static_cast<std::size_t>(size));destroy(value);
    value->size=size;
    if(size<16) std::memcpy(value->storage.inline_bytes,copy.c_str(),size+1);
    else {auto* owned=new char[size+1];std::memcpy(owned,copy.c_str(),size+1);value->storage.pointer=owned;value->capacity=size;}
    return value;
}
struct Row {EngineString text{};bool synthetic=false;char padding[7]{};};
static_assert(sizeof(Row)==40);
struct Rows {const std::byte* begin;const std::byte* end;const std::byte* capacity;};
Rows cache{};std::uint32_t moved=0;int redraws=0;
const Rows* rows(void*) {return &cache;}
void move(void*,std::uint32_t offset) {moved=offset;}
void redraw(void*) {++redraws;}
void test(const std::string& text,const std::vector<std::pair<std::string,bool>>& lines) {
    alignas(void*) std::array<std::byte,0x160> buffer{};
    std::array<void*,70> methods{};
    methods[0x1a0/8]=reinterpret_cast<void*>(rows);methods[0x208/8]=reinterpret_cast<void*>(redraw);
    auto* vtable=methods.data();std::memcpy(buffer.data(),&vtable,sizeof(vtable));
    auto* document=reinterpret_cast<EngineString*>(buffer.data()+0x30);
    auto* selection=reinterpret_cast<EngineString*>(buffer.data()+0x70);
    document->capacity=selection->capacity=15;
    assign(document,text.data(),text.size());assign(selection,"old",3);
    std::vector<Row> native_rows(lines.size());
    for(std::size_t i=0;i<lines.size();++i) {
        native_rows[i].text.capacity=15;
        assign(&native_rows[i].text,lines[i].first.data(),lines[i].first.size());native_rows[i].synthetic=lines[i].second;
    }
    const auto* begin=reinterpret_cast<const std::byte*>(native_rows.data());
    cache={begin,begin+native_rows.size()*sizeof(Row),begin+native_rows.size()*sizeof(Row)};
    for(int repeat=0;repeat<2;++repeat) {
        const auto before=redraws;native_edit_select_all(buffer.data());
        const auto state=native_edit_state(buffer.data());
        check(state.text==text,"selection never rewrites the document");
        check(state.selection.anchor==0&&state.selection.caret==text.size(),"entire multiline document selected at complete boundaries");
        check(std::string_view(selection->data(),selection->size)==text,"native clipboard selection contains exact document bytes");
        check(moved==text.size()&&redraws==before+1,"native movement and cursor redraw run exactly once");
        check((buffer[0x90]!=std::byte{})==!text.empty(),"empty documents have no stale selection");
    }
    destroy(document);destroy(selection);for(auto& row:native_rows) destroy(&row.text);
}
}
int main() {
    using namespace eu4unicode;
    configure_native_editor_text(NativeEditorTextApi{assign,destroy,move});
    test("",{});
    test(u8"A中𠀀é👩‍👩‍👧‍👦",{{u8"A中𠀀é👩‍👩‍👧‍👦",false}});
    test(u8"中文\r\nسلام\n",{{u8"中文\r\n",false},{u8"سلام\n",false}});
    test(u8"A中𠀀B",{{u8"A中\n",true},{u8"𠀀B",false}});
    std::cout<<"Native select-all, clipboard bytes, multiline/soft wraps and empty documents passed\n";
}
