#include "native_paragraph.hpp"
#include "native_font_atlas.hpp"
#include "unicode_text.hpp"
#include "formatted_text.hpp"
#include "formatted_paragraph.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace eu4unicode {
NativeTextWidth original_text_width=nullptr;
NativeTextHeight original_text_height=nullptr;
NativeParagraphColor native_paragraph_color=nullptr;
NativeLineAdvance plan_paragraph_line(const ShapedParagraph& paragraph,std::size_t line,float scale) {
    if(!std::isfinite(scale)||scale<=0) throw std::invalid_argument("Invalid paragraph advance scale");
    const auto& metrics=paragraph.lines().at(line);
    std::vector<const InlinePlacement*> objects;
    for(const auto& object:paragraph.objects())
        if(object.text_start>=metrics.text_start&&object.text_start<metrics.text_start+metrics.text_length)
            objects.push_back(&object);
    std::stable_sort(objects.begin(),objects.end(),[](const auto* left,const auto* right){return left->x<right->x;});
    NativeLineAdvance result{};
    auto advance=[&](float target) {
        const auto delta=std::round(target-result.pixels/scale);
        if(!std::isfinite(delta)||delta<-32768||delta>32767)
            throw std::length_error("Native paragraph advance exceeds capacity");
        const auto value=static_cast<int>(delta);result.pixels+=value*scale;return value;
    };
    for(const auto* object:objects) {
        result.icons.push_back({object->id,advance(object->x)});
        // The native font returns integer client pixels for an icon. Retain
        // that contract at fractional UI scales rather than rounding per run.
        result.pixels+=std::round(paragraph.content().icons().at(object->id).advance*scale);
    }
    result.finish=advance(std::ceil(metrics.width));
    return result;
}
namespace {
void(*logger)(const char*)=nullptr;
std::atomic<bool> reported_error{false};
std::atomic<bool> reported_shaping{false};
void paragraph_failure() noexcept {
    if(logger&&!reported_error.exchange(true)) logger("Native paragraph preparation failed; existing text path retained.");
}
struct Scope {
    bool lookup_enabled=true;
    void* const* table=nullptr;
    std::shared_ptr<const NativeParagraph> paragraph;
    EngineString draw{};
};
thread_local std::array<Scope,32> scopes;
thread_local std::size_t depth=0;
float native_scale(void* font) { return *reinterpret_cast<const float*>(static_cast<const std::byte*>(font)+0x968); }
bool transport_text(void* font,std::string_view text,bool formatted) noexcept {
    const auto table=reinterpret_cast<void* const*>(static_cast<const std::byte*>(font)+0x120);
    bool token=false;
    for(std::size_t offset=0;offset<text.size();) {
        const auto unit=native_text_unit(text,offset,formatted);
        if(unit.kind==TextUnitKind::glyph&&unit.scalar!='\n') {
            if(!find_paragraph_glyph(table,unit.scalar)) return false;
            token=true;
        }
        offset=unit.end;
    }
    return token;
}
struct LookupMask {
    Scope* scope=depth&&depth<=scopes.size()?&scopes[depth-1]:nullptr;
    bool previous=scope?scope->lookup_enabled:false;
    LookupMask() { if(scope) scope->lookup_enabled=false; }
    ~LookupMask() { if(scope) scope->lookup_enabled=previous; }
};
const EngineString* begin_paragraph(void* font,const EngineString* source,float pixels,bool wrap,bool formatted) noexcept {
    // An empty frame masks any parent invocation, including another font.
    const auto index=depth++;
    if(index>=scopes.size()) return source;
    auto& scope=scopes[index];scope={};
    try {
        if(!dynamic_font(font)||!source||pixels<=0||!std::isfinite(pixels)) return source;
        const auto text=std::string_view(source->data(),static_cast<std::size_t>(source->size));
        if(!needs_native_paragraph_shaping(text,formatted)) return source;
        const auto scale=native_scale(font);
        if(!std::isfinite(scale)||scale<=0) return source;
        const auto width=static_cast<float>(pixels)/scale;
        scope.paragraph=font_paragraph_geometry(font,text,width,wrap,formatted);
        if(!scope.paragraph) return source;
        scope.table=reinterpret_cast<void* const*>(static_cast<const std::byte*>(font)+0x120);
        scope.draw.storage.pointer=scope.paragraph->draw_text.c_str();
        scope.draw.size=scope.paragraph->draw_text.size();
        scope.draw.capacity=(std::max)(scope.draw.size,std::uint64_t{16});
        if(logger&&!reported_shaping.exchange(true)) logger("Native paragraph shaping active for UI text.");
        return &scope.draw;
    } catch(...) { paragraph_failure();scope={};return source; }
}
}
const EngineString* begin_native_paragraph(void* font,const EngineString* source,const int* box,int inset,bool formatted) noexcept {
    const auto pixels=box&&inset>=0?static_cast<float>(static_cast<std::int64_t>(box[2]?box[2]:320)-2ll*inset):0.f;
    return begin_paragraph(font,source,pixels,true,formatted);
}
const EngineString* begin_native_button_paragraph(void* font,const EngineString* source,int width,const int* margin,bool formatted) noexcept {
    const auto pixels=margin?static_cast<float>(static_cast<std::int64_t>(width?width:320)-2ll*margin[0]):0.f;
    return begin_paragraph(font,source,pixels,true,formatted);
}
const EngineString* begin_native_popup_paragraph(void* font,const EngineString* source,int width) noexcept {
    const auto scale=dynamic_font(font)?native_scale(font):1.f;
    return begin_paragraph(font,source,width<0?32767.f*scale:static_cast<float>(width),width>=0,true);
}
void end_native_paragraph() noexcept {
    if(!depth) return;
    const auto index=--depth;
    if(index<scopes.size()) scopes[index]={};
}
NativeGlyph* find_paragraph_glyph(void* const* table,std::uint32_t token) noexcept {
    if(!depth||depth>scopes.size()||!table) return nullptr;
    const auto& scope=scopes[depth-1];
    if(!scope.lookup_enabled||!scope.paragraph||!scope.table||
       (table!=scope.table&&table[0x41]!=scope.table[0x41])||token<scope.paragraph->first_token) return nullptr;
    const auto index=static_cast<std::size_t>(token-scope.paragraph->first_token);
    return index<scope.paragraph->glyphs.size()?scope.paragraph->glyphs[index]:nullptr;
}
int measure_paragraph_text(void* font,const char* source,int length,bool formatted) {
    try {
        if(source&&dynamic_font(font)) {
            const auto bytes=length<0?strnlen_s(source,32001):static_cast<std::size_t>(length);
            const auto text=std::string_view(source,bytes);
            if(bytes<=32000&&transport_text(font,text,formatted)) return original_text_width(font,source,length,formatted);
            if(needs_native_paragraph_shaping(text,formatted)) {
                const auto paragraph=font_paragraph_layout(font,text,32767,false,formatted);
                const auto scale=native_scale(font);
                if(paragraph&&std::isfinite(scale)&&scale>0) {
                    // Match the integer advance used by the native glyph loop;
                    // rounding each scalar would destroy cluster positioning.
                    float width=0;
                    for(std::size_t line=0;line<paragraph->lines().size();++line)
                        width=(std::max)(width,plan_paragraph_line(*paragraph,line,scale).pixels);
                    if(width<static_cast<float>((std::numeric_limits<int>::max)())) return static_cast<int>(width);
                }
            }
        }
    } catch(...) { paragraph_failure(); }
    LookupMask mask;return original_text_width(font,source,length,formatted);
}
int measure_paragraph_height(void* font,const EngineString* source,int width,int height,const int* margin,bool formatted) {
    try {
        if(source&&margin&&dynamic_font(font)) {
            const auto text=std::string_view(source->data(),static_cast<std::size_t>(source->size));
            if(text.size()<=32000&&transport_text(font,text,formatted)) return original_text_height(font,source,width,height,margin,formatted);
            const auto pixels=static_cast<std::int64_t>(width?width:320)-2ll*margin[0];
            const auto scale=native_scale(font);
            if(needs_native_paragraph_shaping(text,formatted)&&pixels>0&&std::isfinite(scale)&&scale>0) {
                const auto paragraph=font_paragraph_layout(font,text,static_cast<float>(pixels)/scale,true,formatted);
                if(paragraph) {
                    const auto line_height=paragraph->lines().front().height*scale;
                    if(line_height<(std::numeric_limits<int>::max)()/501.f)
                        return static_cast<int>(line_height)*static_cast<int>(paragraph->metrics().lines);
                }
            }
        }
    } catch(...) { paragraph_failure(); }
    LookupMask mask;return original_text_height(font,source,width,height,margin,formatted);
}
void configure_paragraph_log(void(*log)(const char*)) noexcept { logger=log;reported_error=false;reported_shaping=false; }
}
