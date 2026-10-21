#include "linux/editor_layout.hpp"
#include "linux/font_pages.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unistd.h>

namespace eu4unicode::linux_native {
namespace {
thread_local void* active_buffer=nullptr;
struct Affinity {
    std::string document;
    std::uint16_t row=0,column=0;
    bool trailing=false;
};
std::mutex affinity_mutex;
std::unordered_map<void*,Affinity> affinities;
SafetyHookInline *caret_hook=nullptr,*anchor_hook=nullptr,*string_caret_hook=nullptr;
SafetyHookInline *input_hook=nullptr,*mouse_hook=nullptr;
std::atomic<std::uint64_t> hits{0},moves{0},carets{0},anchors{0};
std::atomic<std::uint64_t> hit_caret_logs{0};

template<class T> T member(const void* object,std::size_t offset) {
    T value;std::memcpy(&value,static_cast<const char*>(object)+offset,sizeof(T));return value;
}
template<class T> void store(void* object,std::size_t offset,const T& value) {
    std::memcpy(static_cast<char*>(object)+offset,&value,sizeof(T));
}
std::string_view document(void* buffer) {
    return reinterpret_cast<const String*>(static_cast<const char*>(buffer)+0x30)->view();
}
bool eligible(void* buffer) {
    if(buffer!=active_buffer) return false;
    const auto source=document(buffer);
    return source.size()<=32000&&valid_utf8(source);
}
// GetRowNo constructs an owned GNU CString, including wrapped rows. Its
// verified native destructor selects the game's own allocator for long rows.
class Row {
public:
    Row(void* buffer,int index) {
        reinterpret_cast<void(*)(String*,const void*,int)>(0x20d4d76)(&value_,buffer,index);
    }
    ~Row() {reinterpret_cast<void(*)(String*)>(0xd95742)(&value_);}
    std::string_view text() const {return value_.view();}
    Row(const Row&)=delete;
    Row& operator=(const Row&)=delete;
private:
    String value_{};
};
std::shared_ptr<const ShapedParagraph> layout(void* buffer,std::string_view text) {
    if(text.size()>32000||!valid_utf8(text)) return {};
    return font_paragraph_layout(member<void*>(buffer,0x98),text,32767,false,member<bool>(buffer,0xe4));
}
float scale(void* buffer) {
    const auto result=member<float>(member<void*>(buffer,0x98),0x940);
    if(!std::isfinite(result)||result<=0) throw std::domain_error("Invalid editor font scale");
    return result;
}
bool trailing(void* buffer,std::uint16_t row,std::uint16_t column) {
    const std::lock_guard lock(affinity_mutex);
    const auto found=affinities.find(buffer);
    return found!=affinities.end()&&found->second.row==row&&found->second.column==column&&
        found->second.document==document(buffer)&&found->second.trailing;
}
void remember(void* buffer,std::uint16_t row,std::uint16_t column,bool value) {
    // Build the snapshot before modifying any native cursor field.
    Affinity next{std::string(document(buffer)),row,column,value};
    const std::lock_guard lock(affinity_mutex);
    affinities.insert_or_assign(buffer,std::move(next));
}
void rejected(const char* operation) noexcept {
    static std::atomic<unsigned> count{0};
    if(count.fetch_add(1)<4) log("Linux shaped editor %s rejected",operation);
}
std::uint32_t position(void* buffer,std::uint32_t native,std::string_view text,bool anchor) {
    const auto row=member<std::uint16_t>(buffer,anchor?0x94:0x56);
    const auto column=member<std::uint16_t>(buffer,anchor?0x92:0x54);
    const auto computed=editor_layout_x(buffer,text,anchor);
    if(!computed) return native;
    const auto x=*computed;
    if(!std::isfinite(x)||x>UINT16_MAX) throw std::out_of_range("Editor cursor exceeds native pixel range");
    // Keep the native vertical row/padding contract and update its pixel cache.
    const auto result=(native&0xffff0000u)|static_cast<std::uint16_t>(std::max(0.f,x));
    store(buffer,anchor?0xa6:0xa2,result);
    auto& counter=anchor?anchors:carets;
    const auto count=counter.fetch_add(1)+1;
    if(count<=4) log("Linux shaped editor %s row=%u column=%u x=%u trailing=%d",
        anchor?"anchor":"caret",row,column,result&0xffffu,!anchor&&trailing(buffer,row,column));
    const auto hit=hits.load();
    if(!anchor&&hit&&hit<=16&&hit_caret_logs.exchange(hit)!=hit)
        log("Linux shaped editor hit-caret column=%u x=%u trailing=%d tid=%ld",column,result&0xffffu,
            trailing(buffer,row,column),static_cast<long>(gettid()));
    return result;
}
std::uint32_t pixels(void* buffer,bool anchor) {
    const auto native=(anchor?anchor_hook:caret_hook)->call<std::uint32_t>(buffer);
    if(!eligible(buffer)) return native;
    try {
        const Row row(buffer,member<std::uint16_t>(buffer,anchor?0x94:0x56));
        return position(buffer,native,row.text(),anchor);
    } catch(...) {rejected("position");return native;}
}
std::uint32_t caret(void* buffer) {return pixels(buffer,false);}
std::uint32_t anchor(void* buffer) {return pixels(buffer,true);}
std::uint32_t string_caret(void* buffer,const String* source) {
    const auto native=string_caret_hook->call<std::uint32_t>(buffer,source);
    if(!eligible(buffer)) return native;
    try {return position(buffer,native,source->view(),false);}
    catch(...) {rejected("supplied-row position");return native;}
}
void input(void* editor,void* event) {
    EditorLayoutScope scope(editor);input_hook->call<void>(editor,event);
}
void mouse(void* editor,int* point) {
    EditorLayoutScope scope(editor);mouse_hook->call<void>(editor,point);
}
bool move(void* buffer,bool right) {
    if(!eligible(buffer)) return false;
    try {
        auto row=member<std::uint16_t>(buffer,0x56);
        const auto column=member<std::uint16_t>(buffer,0x54);
        const Row source(buffer,row);
        if(column>source.text().size()) return false;
        const auto paragraph=layout(buffer,source.text());
        if(!paragraph) return false;
        const auto affinity_before=trailing(buffer,row,column);
        auto target=paragraph->move_caret(column,affinity_before,right);
        if(target.byte_offset==column&&target.trailing==affinity_before) {
            // Native row count is the last valid row index, not a cardinality.
            const auto last=reinterpret_cast<int(*)(void*)>(0x20d4a38)(buffer);
            if((right&&row<last)||(!right&&row)) {
                row=static_cast<std::uint16_t>(right?row+1:row-1);
                const Row next(buffer,row);
                if(const auto next_layout=layout(buffer,next.text())) {
                    const auto hit=next_layout->hit_test(right?-1.f:32767.f,0);
                    target.byte_offset=hit.byte_offset;target.trailing=hit.trailing;
                } else {
                    target.byte_offset=right?0:next.text().size();target.trailing=false;
                }
            }
        }
        if(target.byte_offset>UINT16_MAX) throw std::out_of_range("Editor movement exceeds native column range");
        const auto next=static_cast<std::uint16_t>(target.byte_offset);
        remember(buffer,row,next,target.trailing);
        store(buffer,0x54,next);store(buffer,0x56,row);
        const auto count=moves.fetch_add(1)+1;
        if(count<=8) log("Linux shaped editor move right=%d row=%u from=%u to=%u trailing=%d",right,row,column,next,target.trailing);
        return true;
    } catch(...) {rejected("movement");return false;}
}
void left(safetyhook::Context& context) {
    if(move(reinterpret_cast<void*>(context.rbx),false)) context.rip=0x20d7876;
}
void right(safetyhook::Context& context) {
    if(move(reinterpret_cast<void*>(context.rbx),true)) context.rip=0x20d793d;
}
}
EditorLayoutScope::EditorLayoutScope(void* editor) noexcept:previous_(active_buffer) {
    active_buffer=editor&&!member<bool>(editor,0x262)?static_cast<char*>(editor)+0xc8:nullptr;
}
EditorLayoutScope::~EditorLayoutScope() {active_buffer=previous_;}
void forget_editor_layout(void* editor) noexcept {
    forget_editor_buffer(static_cast<char*>(editor)+0xc8);
}
void forget_editor_buffer(void* buffer) noexcept {
    const std::lock_guard lock(affinity_mutex);
    affinities.erase(buffer);
}
std::optional<float> editor_layout_x(void* buffer,std::string_view text,bool anchor) {
    if(!eligible(buffer)) return {};
    const auto row=member<std::uint16_t>(buffer,anchor?0x94:0x56);
    const auto column=member<std::uint16_t>(buffer,anchor?0x92:0x54);
    if(column>text.size()) return {};
    const auto paragraph=layout(buffer,text);
    if(!paragraph) return {};
    const auto caret=paragraph->caret(column,!anchor&&trailing(buffer,row,column));
    return std::round(caret.x*scale(buffer))+member<int>(member<void*>(buffer,0x98),0x30)+(anchor?3:0);
}
std::optional<std::vector<SelectionRegion>> editor_layout_selection(void* buffer,std::string_view text) {
    if(!eligible(buffer)||member<std::uint16_t>(buffer,0x56)!=member<std::uint16_t>(buffer,0x94)) return {};
    const auto first=member<std::uint16_t>(buffer,0x54),last=member<std::uint16_t>(buffer,0x92);
    if(first>text.size()||last>text.size()) return {};
    const auto paragraph=layout(buffer,text);
    if(!paragraph) return {};
    auto regions=paragraph->selection(std::min(first,last),std::max(first,last));
    const auto factor=scale(buffer);
    for(auto& region:regions) {
        const auto right=std::round((region.x+region.width)*factor);
        region.x=std::round(region.x*factor);region.width=right-region.x;
        region.y=std::round(region.y*factor);region.height=std::round(region.height*factor);
    }
    return regions;
}
bool editor_layout_hit(void* buffer,const String* source,const int* point) {
    if(!eligible(buffer)||point[0]<0||point[1]<0) return false;
    const auto paragraph=layout(buffer,source->view());
    if(!paragraph) return false;
    const auto hit=paragraph->hit_test(point[0]/scale(buffer),0);
    if(hit.byte_offset>UINT16_MAX) throw std::out_of_range("Editor hit exceeds native column range");
    const auto column=static_cast<std::uint16_t>(hit.byte_offset);
    remember(buffer,0,column,hit.trailing);
    const std::array<std::uint16_t,2> point_in_text{column,0};
    std::memcpy(static_cast<char*>(buffer)+0x54,point_in_text.data(),sizeof(point_in_text));
    const auto count=hits.fetch_add(1)+1;
    if(count<=8) log("Linux shaped editor hit x=%d column=%u trailing=%d tid=%ld",point[0],column,hit.trailing,
        static_cast<long>(gettid()));
    return true;
}
void prepare_editor_layout(Hooks& hooks) {
    for(const auto address:{0x20d4d76,0xd95742,0x20d4a38,0x20d7876,0x20d793d}) verify_site(address);
    caret_hook=&hooks.add_inline(0x20d4f5a,reinterpret_cast<void*>(caret));
    anchor_hook=&hooks.add_inline(0x20d4b58,reinterpret_cast<void*>(anchor));
    string_caret_hook=&hooks.add_inline(0x20d53cc,reinterpret_cast<void*>(string_caret));
    input_hook=&hooks.add_inline(0x2066750,reinterpret_cast<void*>(input));
    mouse_hook=&hooks.add_inline(0x206684c,reinterpret_cast<void*>(mouse));
    hooks.add_mid(0x20d7844,left);hooks.add_mid(0x20d78de,right);
}
void report_editor_layout_counts() noexcept {
    log("Linux shaped editor hits=%llu moves=%llu carets=%llu anchors=%llu",
        static_cast<unsigned long long>(hits.load()),static_cast<unsigned long long>(moves.load()),
        static_cast<unsigned long long>(carets.load()),static_cast<unsigned long long>(anchors.load()));
}
}
