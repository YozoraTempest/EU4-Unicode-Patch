#include "linux/paragraph.hpp"
#include "unicode_text.hpp"
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>

namespace eu4unicode::linux_native {
namespace {
thread_local ParagraphScope* current=nullptr;
std::atomic<bool> reported_failure=false;
std::atomic<unsigned> reported_renderers=0;
struct EditorView {void* font;std::string text;ParagraphViewport viewport;};
std::mutex view_mutex;
std::shared_ptr<const EditorView> editor_view;
bool same_table(void* const* left,void* const* right) noexcept {
    return left&&right&&(left==right||(left[0x41]&&left[0x41]==right[0x41]));
}
}
void publish_editor_view(void* font,std::string text,ParagraphViewport viewport) {
    auto next=std::make_shared<const EditorView>(EditorView{font,std::move(text),viewport});
    const std::lock_guard lock(view_mutex);editor_view=std::move(next);
}
void clear_editor_view() noexcept {
    const std::lock_guard lock(view_mutex);editor_view.reset();
}
void report_paragraph_failure(const char* operation,const char* message) noexcept {
    if(!reported_failure.exchange(true)) log("Linux paragraph %s failed: %s",operation,message);
}
bool paragraph_transport_source(void* font,std::string_view text) noexcept {
    if(!font||!current||!current->geometry_||text.empty()||
       !same_table(current->table_,reinterpret_cast<void* const*>(static_cast<const char*>(font)+0x100))||
       current->text_.find(text)==std::string_view::npos) return false;
    for(std::size_t offset=0;offset<text.size();) {
        const auto unit=decode(text.substr(offset));
        if(unit.valid&&unit.value>=current->geometry_->first_token&&
           unit.value-current->geometry_->first_token<current->geometry_->glyphs.size()) return true;
        offset+=unit.bytes;
    }
    return false;
}
ParagraphScope::ParagraphScope(void* font,const String* source):previous_(current),
    table_(font?reinterpret_cast<void* const*>(static_cast<const char*>(font)+0x100):nullptr) {
    if(source&&paragraph_transport_source(font,source->view())) {
        geometry_=current->geometry_;text_=source->view();native_=borrowed_string(text_);
    }
    current=this;
}
ParagraphScope::~ParagraphScope() {current=previous_;}
const String* ParagraphScope::prepare(const String* source,float pixels,bool wrap,bool formatted,const char* renderer) noexcept {
    if(geometry_) return &native_;
    if(!source||!table_||!std::isfinite(pixels)||pixels<=0) return source;
    try {
        auto font=const_cast<char*>(reinterpret_cast<const char*>(table_)-0x100);
        float scale=0;std::memcpy(&scale,static_cast<const char*>(font)+0x940,sizeof(scale));
        if(!std::isfinite(scale)||scale<=0||pixels/scale>32767) return source;
        std::optional<ParagraphViewport> viewport;
        {
            const std::lock_guard lock(view_mutex);
            if(editor_view&&editor_view->font==font&&editor_view->text==source->view()) viewport=editor_view->viewport;
        }
        geometry_=font_paragraph_geometry(font,source->view(),viewport?32767:pixels/scale,viewport?false:wrap,formatted,
            viewport?&*viewport:nullptr);
        if(!geometry_) return source;
        text_=geometry_->text;
        if(formatted) {
            normalized_.reserve(text_.size());
            for(std::size_t offset=0;offset<text_.size();) {
                const auto unit=decode(text_.substr(offset));
                if(unit.valid&&unit.bytes==2&&(unit.value==0xa3||unit.value==0xa4||unit.value==0xa7))
                    normalized_.push_back(static_cast<char>(unit.value));
                else normalized_.append(text_.substr(offset,unit.bytes));
                offset+=unit.bytes;
            }
            text_=normalized_;
        }
        native_=borrowed_string(text_);
        const unsigned bit=renderer[0]=='s'?1u:renderer[0]=='t'?2u:4u;
        if(!(reported_renderers.fetch_or(bit)&bit))
            log("Linux shaped paragraph renderer=%s lines=%zu tokens=%zu bytes=%zu width=%.2f height=%.2f scale=%.2f",
                renderer,geometry_->layout->lines().size(),geometry_->glyphs.size(),text_.size(),
                geometry_->layout->metrics().width,geometry_->layout->metrics().height,scale);
        return &native_;
    } catch(const std::exception& error) {
        report_paragraph_failure(renderer,error.what());geometry_.reset();text_={};return source;
    }
}
NativeGlyph* find_paragraph_glyph(void* const* table,std::uint32_t token) noexcept {
    if(!current||!current->geometry_||!same_table(table,current->table_)||token<current->geometry_->first_token) return nullptr;
    const auto index=token-current->geometry_->first_token;
    return index<current->geometry_->glyphs.size()?const_cast<NativeGlyph*>(&current->geometry_->glyphs[index]):nullptr;
}
}
