#include "native_paragraph.hpp"
#include "native_font_atlas.hpp"
#include "unicode_text.hpp"
#include "formatted_text.hpp"
#include "formatted_paragraph.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
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
std::atomic<unsigned> reported_shaping{0};
enum class Renderer : unsigned { main,button,popup };
void report_shaping(Renderer renderer,std::string_view text,bool formatted) {
    if(!logger) return;
    bool commands=false;
    for(std::size_t offset=0;offset<text.size();) {
        const auto unit=native_text_unit(text,offset,true);
        if(unit.kind!=TextUnitKind::glyph) { commands=true;break; }
        offset=unit.end;
    }
    const auto state=static_cast<unsigned>(renderer)*4+(formatted?1u:0u)+(commands?2u:0u);
    const auto bit=1u<<state;
    if(reported_shaping.fetch_or(bit)&bit) return;
    constexpr const char* names[]{"main","button","popup"};
    char message[128];
    std::snprintf(message,sizeof(message),"Native paragraph shaping active: renderer=%s formatted=%s commands=%s.",
        names[static_cast<unsigned>(renderer)],formatted?"yes":"no",commands?"yes":"no");
    logger(message);
}
void paragraph_failure() noexcept {
    if(logger&&!reported_error.exchange(true)) logger("Native paragraph preparation failed; existing text path retained.");
}
struct Scope {
    bool lookup_enabled=true;
    bool map=false;
    bool native_metrics=false;
    void* const* table=nullptr;
    std::shared_ptr<const NativeParagraph> paragraph;
    EngineString draw{};
    std::vector<std::shared_ptr<const NativeParagraph>> map_paragraphs;
    std::vector<std::unique_ptr<std::array<std::byte,0x30>>> map_labels;
};
thread_local std::array<Scope,32> scopes;
thread_local std::size_t depth=0;
float native_scale(void* font) { return *reinterpret_cast<const float*>(static_cast<const std::byte*>(font)+0x968); }
bool retained_native_metrics(void* font) noexcept {
    if(!font||!depth||depth>scopes.size()) return false;
    const auto& scope=scopes[depth-1];
    const auto table=reinterpret_cast<void* const*>(static_cast<const std::byte*>(font)+0x120);
    return scope.native_metrics&&scope.table&&(table==scope.table||table[0x41]==scope.table[0x41]);
}
bool transport_text(void* font,std::string_view text,bool formatted) noexcept {
    const auto table=reinterpret_cast<void* const*>(static_cast<const std::byte*>(font)+0x120);
    bool token=false;
    for(std::size_t offset=0;offset<text.size();) {
        const auto unit=native_text_unit(text,offset,formatted);
        if(unit.kind==TextUnitKind::glyph&&unit.scalar!='\n') {
            if(find_paragraph_glyph(table,unit.scalar)) token=true;
            else if(!(unit.scalar<=255?table[unit.scalar]:find_unicode_glyph(table,unit.scalar))) return false;
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
const EngineString* begin_paragraph(void* font,const EngineString* source,float pixels,bool wrap,bool formatted,Renderer renderer) noexcept {
    // An empty frame masks any parent invocation, including another font.
    const auto index=depth++;
    if(index>=scopes.size()) return source;
    auto& scope=scopes[index];scope={};
    try {
        if(!source||!font) return source;
        const auto text=std::string_view(source->data(),static_cast<std::size_t>(source->size));
        // The popup renderer has no country-flag or native-symbol branch.
        // Retain its drawing and measurement contract for these commands.
        if(renderer==Renderer::popup&&formatted) for(std::size_t offset=0;offset<text.size();) {
            const auto unit=native_text_unit(text,offset,true);
            if(unit.kind==TextUnitKind::flag||unit.kind==TextUnitKind::symbol) {
                scope.native_metrics=true;scope.table=reinterpret_cast<void* const*>(static_cast<const std::byte*>(font)+0x120);
                return source;
            }
            offset=unit.end;
        }
        if(!dynamic_font(font)||pixels<=0||!std::isfinite(pixels)) return source;
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
        report_shaping(renderer,text,formatted);
        return &scope.draw;
    } catch(...) { paragraph_failure();scope={};return source; }
}
}
const EngineString* begin_native_paragraph(void* font,const EngineString* source,const int* box,int inset,bool formatted) noexcept {
    const auto pixels=box&&inset>=0?static_cast<float>(static_cast<std::int64_t>(box[2]?box[2]:320)-2ll*inset):0.f;
    return begin_paragraph(font,source,pixels,true,formatted,Renderer::main);
}
const EngineString* begin_native_button_paragraph(void* font,const EngineString* source,int width,const int* margin,bool formatted) noexcept {
    const auto pixels=margin?static_cast<float>(static_cast<std::int64_t>(width?width:320)-2ll*margin[0]):0.f;
    return begin_paragraph(font,source,pixels,true,formatted,Renderer::button);
}
const EngineString* begin_native_button_paragraph_arguments(void* font,const EngineString* source,const std::byte* arguments) noexcept {
    int width=0;const int* margin=nullptr;
    std::memcpy(&width,arguments,sizeof(width));
    std::memcpy(&margin,arguments+0x10,sizeof(margin));
    // The native formatter reads rbp+2210, relative to width at rbp+21d8.
    // rbp+21f8 is the independent truncation flag, not the format flag.
    const auto formatted=arguments[0x38]!=std::byte{0};
    return begin_native_button_paragraph(font,source,width,margin,formatted);
}
const EngineString* begin_native_popup_paragraph(void* font,const EngineString* source,int width) noexcept {
    const auto scale=dynamic_font(font)?native_scale(font):1.f;
    return begin_paragraph(font,source,width<0?32767.f*scale:static_cast<float>(width),width>=0,true,Renderer::popup);
}
void end_native_paragraph() noexcept {
    if(!depth) return;
    const auto index=--depth;
    if(index<scopes.size()) scopes[index]={};
}
void begin_native_map_paragraph() noexcept {
    const auto index=depth++;
    if(index<scopes.size()) { scopes[index]={};scopes[index].map=true; }
}
const EngineString* prepare_native_map_paragraph(void* font,const EngineString* source) noexcept {
    if(!source||!depth||depth>scopes.size()||!scopes[depth-1].map) return source;
    auto& scope=scopes[depth-1];
    try {
        const auto text=std::string_view(source->data(),static_cast<std::size_t>(source->size));
        if(!dynamic_map_font(font)||text.find_first_of("\r\n")!=std::string_view::npos||scope.map_paragraphs.size()>=4096) return source;
        const auto geometry=font_paragraph_geometry(font,text,32767,false,false);
        if(!geometry) return source;
        if(!scope.table) scope.table=reinterpret_cast<void* const*>(static_cast<const std::byte*>(font)+0x120);
        scope.map_paragraphs.push_back(geometry);
        scope.draw.storage.pointer=geometry->draw_text.c_str();scope.draw.size=geometry->draw_text.size();
        scope.draw.capacity=(std::max)(scope.draw.size,std::uint64_t{16});
        return &scope.draw;
    } catch(...) { paragraph_failure();return source; }
}
const void* prepare_native_map_label(void* font,const void* text_block) noexcept {
    if(!text_block) return text_block;
    const auto source=reinterpret_cast<const EngineString*>(static_cast<const std::byte*>(text_block)+0x10);
    const auto draw=prepare_native_map_paragraph(font,source);
    if(draw==source) return text_block;
    try {
        auto copy=std::make_unique<std::array<std::byte,0x30>>();
        std::memcpy(copy->data(),text_block,copy->size());std::memcpy(copy->data()+0x10,draw,sizeof(*draw));
        const auto result=copy->data();scopes[depth-1].map_labels.push_back(std::move(copy));return result;
    } catch(...) { paragraph_failure();return text_block; }
}
bool native_map_paragraph_active() noexcept {
    return depth&&depth<=scopes.size()&&scopes[depth-1].map&&!scopes[depth-1].map_paragraphs.empty();
}
NativeGlyph* find_paragraph_glyph(void* const* table,std::uint32_t token) noexcept {
    if(!depth||depth>scopes.size()||!table) return nullptr;
    const auto& scope=scopes[depth-1];
    if(!scope.lookup_enabled||!scope.table||(table!=scope.table&&table[0x41]!=scope.table[0x41])) return nullptr;
    auto find=[token](const std::shared_ptr<const NativeParagraph>& paragraph)->NativeGlyph* {
        if(!paragraph||token<paragraph->first_token) return nullptr;
        const auto index=static_cast<std::size_t>(token-paragraph->first_token);
        return index<paragraph->glyphs.size()?paragraph->glyphs[index]:nullptr;
    };
    if(const auto glyph=find(scope.paragraph)) return glyph;
    for(const auto& paragraph:scope.map_paragraphs) if(const auto glyph=find(paragraph)) return glyph;
    return nullptr;
}
int measure_paragraph_text(void* font,const char* source,int length,bool formatted) {
    if(retained_native_metrics(font)) return original_text_width(font,source,length,formatted);
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
    if(retained_native_metrics(font)) return original_text_height(font,source,width,height,margin,formatted);
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
void configure_paragraph_log(void(*log)(const char*)) noexcept { logger=log;reported_error=false;reported_shaping=0; }
}
