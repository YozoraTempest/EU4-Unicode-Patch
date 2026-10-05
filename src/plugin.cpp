#include <windows.h>
#include <bcrypt.h>
#include <MinHook.h>
#include "unicode_text.hpp"
#include "unicode_services.hpp"
#include "formatted_text.hpp"
#include "unicode_editor.hpp"
#include "native_editor_text.hpp"
#include "native_editor_presentation.hpp"
#include "native_text_event.hpp"
#include "unicode_search.hpp"
#include "unicode_pinyin.hpp"
#include "native_search.hpp"
#include "native_steam_presence.hpp"
#include "native_script_bom.hpp"
#include "glyph_registry.hpp"
#include "native_ime.hpp"
#include "native_editor_selection.hpp"
#include "native_font_atlas.hpp"
#include "native_font_draw.hpp"
#include "font_assets.hpp"
#include "font_atlas_assets.hpp"
#include <array>
#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <intrin.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
HANDLE log_file=INVALID_HANDLE_VALUE;
std::byte* image=nullptr;
std::atomic<bool> patch_enabled{false};
thread_local std::uint32_t last_slot=0;
thread_local std::uint32_t button_extra=0;
thread_local std::uint32_t button_slot=0;
thread_local std::uint32_t last_scalar_bytes=1;
thread_local std::shared_ptr<const eu4unicode::FormattedText> active_line_breaks,button_line_breaks;
thread_local std::shared_ptr<const eu4unicode::FormattedText> popup_line_breaks;
struct CachedFormattedText { std::shared_ptr<const eu4unicode::FormattedText> value; std::size_t bytes; };
thread_local std::unordered_map<std::string,CachedFormattedText> formatted_cache;
thread_local std::size_t formatted_cache_bytes=0;
std::shared_ptr<const eu4unicode::FormattedText> formatted_boundaries(std::string_view text) {
    std::string key(text);
    const auto found=formatted_cache.find(key);
    if(found!=formatted_cache.end()) return found->second.value;
    auto value=std::make_shared<const eu4unicode::FormattedText>(text);
    const auto bytes=key.size()+value->memory_size()+sizeof(CachedFormattedText);
    constexpr std::size_t budget=1024*1024;
    if(bytes<=budget) {
        if(formatted_cache.size()>=256||formatted_cache_bytes>budget-bytes) {
            formatted_cache.clear();formatted_cache_bytes=0;
        }
        formatted_cache.emplace(std::move(key),CachedFormattedText{value,bytes});
        formatted_cache_bytes+=bytes;
    }
    return value;
}
void log(const char* message) {
    if(log_file==INVALID_HANDLE_VALUE) return;
    DWORD written=0;
    WriteFile(log_file,message,static_cast<DWORD>(std::strlen(message)),&written,nullptr);
    WriteFile(log_file,"\r\n",2,&written,nullptr);
    FlushFileBuffers(log_file);
}
using eu4unicode::EngineString;
using SubstringText=EngineString*(*)(const EngineString*,EngineString*,int,int);
SubstringText original_layout_substring=nullptr;
EngineString* layout_substring(const EngineString* source,EngineString* target,int begin,int end) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-
        reinterpret_cast<std::uintptr_t>(image);
    if(eu4unicode::layout_substring_caller(caller)) {
        try {
            const auto boundaries=formatted_boundaries({source->data(),static_cast<std::size_t>(source->size)});
            // The native API takes [begin,end), not a start and length.
            begin=static_cast<int>(boundaries->prefix(static_cast<std::size_t>(std::max(0,begin))));
            end=static_cast<int>(boundaries->prefix(static_cast<std::size_t>(std::max(begin,end))));
        } catch(...) { log("Unicode layout substring failed; fragment excluded.");begin=0;end=0; }
    }
    return original_layout_substring(source,target,begin,end);
}
eu4unicode::NativePresenceConversion original_presence_conversion=nullptr;
EngineString* convert_steam_presence(EngineString* target,const EngineString* source) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-
        reinterpret_cast<std::uintptr_t>(image);
    const auto assign=reinterpret_cast<eu4unicode::NativeStringAssignment>(image+0x95110);
    return eu4unicode::construct_steam_presence(target,source,caller,
        original_presence_conversion,assign);
}
using Transliterate=void(*)(EngineString*);
Transliterate original_transliterate=nullptr;
void transliterate_save_path(EngineString* text) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-
        reinterpret_cast<std::uintptr_t>(image);
    const auto value=std::string_view(text->data(),static_cast<std::size_t>(text->size));
    // Preserve only the observed save-games filesystem path. The virtual
    // filesystem still performs its ordinary illegal-path validation.
    if(caller==0x5ca24b && value.substr(0,11)=="save games/" && eu4unicode::valid_utf8(value)) return;
    original_transliterate(text);
}
eu4unicode::NativeFindText original_find_text=nullptr;
eu4unicode::NativeSearchDistance original_search_distance=nullptr;
std::filesystem::path search_dictionary_path;
void load_search_resources() {
    static std::once_flag loaded;
    std::call_once(loaded,[] {
        const auto config=search_dictionary_path.parent_path()/L"config.ini";
        eu4unicode::set_search_options({
            GetPrivateProfileIntW(L"search",L"typo_tolerance",1,config.c_str())!=0,
            GetPrivateProfileIntW(L"search",L"fuzzy_pinyin",0,config.c_str())!=0});
        try {
            if(!std::filesystem::exists(search_dictionary_path)) return;
            std::ifstream file(search_dictionary_path,std::ios::binary);
            if(!file) throw std::runtime_error("Cannot read pinyin dictionary");
            const std::string content((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
            if(file.bad()) throw std::runtime_error("Cannot read pinyin dictionary");
            eu4unicode::set_pinyin_dictionary(content);
            log("Custom pinyin dictionary loaded.");
        } catch(...) { log("Custom pinyin dictionary rejected; builtin pronunciations retained."); }
    });
}
std::uint64_t find_country_name(const char* name,std::uint64_t length,std::uint64_t start,
                              const char* query,std::uint64_t query_length) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-
        reinterpret_cast<std::uintptr_t>(image);
    if(!eu4unicode::display_find_caller(caller)) return original_find_text(name,length,start,query,query_length);
    try {
        load_search_resources();
        return eu4unicode::find_display_name(caller,name,length,start,query,query_length,original_find_text);
    } catch(...) { log("Unicode display-name search failed; candidate excluded."); return UINT64_MAX; }
}
std::int64_t find_province_distance(const EngineString* name,const EngineString* query) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-
        reinterpret_cast<std::uintptr_t>(image);
    if(!eu4unicode::province_distance_caller(caller)) return original_search_distance(name,query);
    try {
        load_search_resources();
        return eu4unicode::province_search_distance(caller,name,query,original_search_distance);
    } catch(...) { log("Unicode province distance failed; candidate ranked last."); return INT32_MAX/2; }
}
struct LoadContext { int line; bool replace; char padding[11]; void* collection; };
static_assert(offsetof(LoadContext,collection)==16);
using RegisterText=void(*)(void*,const char*,const char*,int,int,bool);
RegisterText register_text=nullptr;
using RepeatText=char*(*)(EngineString*,std::uint64_t,unsigned char);
RepeatText repeat_text=nullptr;
using AppendText=void*(*)(EngineString*,const char*,std::uint64_t);
AppendText append_text=nullptr;
struct KeyEvent { std::uint32_t key,unused,modifiers; };
using AssignText=EngineString*(*)(EngineString*,const char*,std::uint64_t);
AssignText original_assign_text=nullptr;
using FilterText=void(*)(EngineString*,const EngineString*);
FilterText original_filter_text=nullptr;
thread_local bool clipboard_font_filter=false;
void filter_editor_text(EngineString* text,const EngineString* blacklist) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-
        reinterpret_cast<std::uintptr_t>(image);
    const auto value=std::string_view(text->data(),static_cast<std::size_t>(text->size));
    if((caller!=0x1536c37&&!(clipboard_font_filter&&caller==0x15a05ad)) || !eu4unicode::valid_utf8(value)) {
        original_filter_text(text,blacklist); return;
    }
    try {
        const auto filtered=eu4unicode::filter_editor_characters(value,
            std::string_view(blacklist->data(),static_cast<std::size_t>(blacklist->size)));
        auto data=const_cast<char*>(text->data());
        std::memcpy(data,filtered.data(),filtered.size());
        data[filtered.size()]='\0';
        text->size=filtered.size();
    } catch(...) { log("Unicode editor filtering failed; insertion excluded."); text->size=0; const_cast<char*>(text->data())[0]='\0'; }
}
EngineString* assign_editor_prefix(EngineString* target,const char* source,std::uint64_t length) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-
        reinterpret_cast<std::uintptr_t>(image);
    // These three native editor branches assign a byte-limited prefix of a
    // complete NUL-terminated string. All other assignment contracts stay native.
    if(caller==0x153a30b || caller==0x153a71c || caller==0x153a9a0) {
        const auto complete=std::string_view(source,std::strlen(source));
        if(eu4unicode::valid_utf8(complete)) {
            try { length=eu4unicode::grapheme_prefix(complete,static_cast<std::size_t>(length)); }
            catch(...) { log("Unicode editor prefix failed; truncated insertion excluded."); length=0; }
        }
    }
    return original_assign_text(target,source,length);
}
using EditorKey=bool(*)(void*,const KeyEvent*);
EditorKey original_editor_key=nullptr;
using EditorAction=void(*)(void*);
EditorAction original_editor_left=nullptr,original_editor_right=nullptr,original_editor_selection=nullptr;
EditorAction original_editor_paint=nullptr,original_editor_focus=nullptr;
struct InputRect { int x,y,w,h; };
struct EditorImeRectState { void* window=nullptr; void* owner=nullptr; InputRect rect{}; };
thread_local EditorImeRectState editor_ime_rect_state{};
thread_local unsigned editor_focus_depth=0;
void clear_editor_affinity() noexcept;
void paint_editor_selection(void* outer) noexcept;
eu4unicode::PrefixMeasure native_editor_measure(void* widget,std::string_view text);
void forget_editor_history(void* widget) noexcept;
void focus_editor_ime_rect(void* outer) {
    clear_editor_affinity();
    eu4unicode::clear_native_composition();
    editor_ime_rect_state={};
    struct FocusScope {
        FocusScope() { ++editor_focus_depth; }
        ~FocusScope() { --editor_focus_depth; }
    } scope;
    original_editor_focus(outer);
}
void paint_editor_ime_rect(void* outer) {
    eu4unicode::EditorPresentation presentation(outer);
    original_editor_paint(outer);
    paint_editor_selection(outer);
    // Focus performs an immediate paint before the next UI frame has supplied
    // the editor's current parent transform. Publish only normal frame geometry.
    if(editor_focus_depth) return;
    const auto base=static_cast<std::byte*>(outer);
    const auto manager=*reinterpret_cast<std::byte**>(image+0x23494f0);
    if(!manager||base[0x260]!=std::byte{1}||
       *reinterpret_cast<void**>(manager+0x210)!=base+0x1d0) return;
    const auto sprite=*reinterpret_cast<void**>(base+0x1f0);
    const auto font=*reinterpret_cast<void**>(base+0xc8+0x98);
    if(!sprite||!font) return;
    using WindowFocus=void*(*)();
    const auto window=reinterpret_cast<WindowFocus>(image+0x17345f0)();
    if(!window) return;
    int width=0,height=0;
    reinterpret_cast<void(*)(void*,int*,int*)>(image+0x17349f0)(window,&width,&height);
    if(width<=0||height<=0) return;
    struct Position { float x,y; } position{};
    using SpritePosition=Position*(*)(void*,Position*);
    reinterpret_cast<SpritePosition>((*static_cast<void***>(sprite))[0x178/8])(sprite,&position);
    if(!std::isfinite(position.x)||!std::isfinite(position.y)) return;
    using FontHeight=int(*)(void*);
    const auto line_height=reinterpret_cast<FontHeight>((*static_cast<void***>(font))[0x68/8])(font);
    if(line_height<=0) return;
    // The cursor sprite already includes the native text origin, scrolling,
    // alignment and font offsets. Its coordinates use SDL client pixels.
    InputRect rect{
        static_cast<int>(std::clamp(position.x,0.0f,static_cast<float>(width-1))),
        static_cast<int>(std::clamp(position.y,0.0f,static_cast<float>(height-1))),1,
        0};
    rect.h=std::min(line_height,height-rect.y);
    const auto& previous=editor_ime_rect_state.rect;
    if(window==editor_ime_rect_state.window&&outer==editor_ime_rect_state.owner&&
       rect.x==previous.x&&rect.y==previous.y&&rect.w==previous.w&&rect.h==previous.h) return;
    reinterpret_cast<void(*)(const InputRect*)>(image+0x1735940)(&rect);
    editor_ime_rect_state={window,outer,rect};
}
struct EditorPoint { int x,y; };
using EditorPointHit=void(*)(void*,const EngineString*,const EditorPoint*);
using EditorWidthFit=int(*)(void*,const EngineString*);
using EditorWordBreak=int(*)(void*,const EngineString*,int);
EditorPointHit original_editor_point=nullptr;
EditorWidthFit original_editor_width_fit=nullptr;
EditorWordBreak original_editor_word_break=nullptr;
using FontTableDestroy=void(*)(void* const*);
FontTableDestroy original_font_table_destroy=nullptr;
using FontLoad=void(*)(void*);
FontLoad original_font_load=nullptr;
#ifndef EU4_UNICODE_RESEARCH
std::filesystem::path player_font_directory;
// The checked executable's resource resolver returns the first mounted source,
// including archives. Use its selection instead of duplicating launcher/mod order.
bool native_font_source(std::string_view path) {
    using ResolveMount=const std::byte*(*)(const char*);
    const auto resolve=reinterpret_cast<ResolveMount>(image+0x19fad70);
    bool metrics=false,texture=false;
    for(const auto extension:{".fnt",".tga",".dds"}) {
        const auto resource=std::string(path)+extension;
        const auto mount=resolve(resource.c_str());
        if(!mount) continue;
        const auto root=*reinterpret_cast<const char* const*>(mount+8);
        const auto prefix=*reinterpret_cast<const char* const*>(mount+16);
        std::error_code error;
        if(!root||(prefix&&*prefix)||
           !std::filesystem::equivalent(std::filesystem::u8path(root),player_font_directory,error)||error)
            return false;
        if(extension==std::string_view(".fnt")) metrics=true;else texture=true;
    }
    return metrics&&texture;
}
#endif
void load_font_atlas(void* font) {
#ifndef EU4_UNICODE_RESEARCH
    for(const auto offset:{0xe0,0x100}) {
        auto path=reinterpret_cast<EngineString*>(static_cast<std::byte*>(font)+offset);
        const auto requested=std::string_view(path->data(),static_cast<std::size_t>(path->size));
        const auto replacement=eu4unicode::player_font_path(requested);
        if(replacement.empty()) continue;
        try {
            if(!native_font_source(requested)) {
                char message[256];std::snprintf(message,sizeof(message),"Mod font retained: %.*s",static_cast<int>(requested.size()),requested.data());log(message);
                continue;
            }
            // Generate only after DLL initialization, outside the loader lock.
            eu4unicode::ensure_player_font_atlas(player_font_directory,replacement);
            // Use the native allocator for both normal and enlarged UI paths.
            reinterpret_cast<AssignText>(image+0x95110)(path,replacement.data(),replacement.size());
        } catch(const std::exception& error) { log(error.what()); }
    }
#endif
    original_font_load(font);
#ifndef EU4_UNICODE_RESEARCH
    const auto object=static_cast<std::byte*>(font);
    const auto context=*reinterpret_cast<const std::byte* const*>(object+0x48);
    const auto large=reinterpret_cast<const EngineString*>(object+0x100);
    const auto threshold=*reinterpret_cast<const float*>(image+0x1dc161c);
    const auto selected=reinterpret_cast<const EngineString*>(object+
        (*reinterpret_cast<const float*>(context+0x32600)>=threshold&&large->size?0x100:0xe0));
    const auto path=std::string_view(selected->data(),static_cast<std::size_t>(selected->size));
    try {
        eu4unicode::register_font_atlas(font,path);
    } catch(const std::exception& error) { log(error.what()); }
#else
    eu4unicode::register_font_atlas(font);
#endif
}
void destroy_font_table(void* const* table) {
    eu4unicode::release_font_atlas(table);
    eu4unicode::release_unicode_font(table);
    original_font_table_destroy(table);
}
struct EditorAffinity {
    void* owner=nullptr;
    std::string text;
    std::size_t offset=0;
    bool trailing=false;
};
thread_local EditorAffinity editor_affinity;
void clear_editor_affinity() noexcept { editor_affinity={}; }
std::shared_ptr<const eu4unicode::ShapedParagraph> editor_paragraph(void* widget,std::string_view value) {
    if(!eu4unicode::needs_paragraph_shaping(value)) return {};
    const auto font=*reinterpret_cast<void* const*>(static_cast<const std::byte*>(widget)+0x98);
    return eu4unicode::font_paragraph_layout(font,value,32767,false,false);
}
EditorAction original_editor_text=nullptr;
using EditorLines=void(*)(void*,int);
EditorLines original_editor_lines=nullptr;
void draw_editor_text(void* outer) { eu4unicode::EditorPresentation presentation(outer);original_editor_text(outer); }
void draw_editor_lines(void* outer,int index) {
    eu4unicode::EditorPresentation presentation(outer);
    original_editor_lines(outer,index);
}
bool editor_trailing(void* widget,std::string_view value,std::size_t offset) {
    return editor_affinity.owner==widget&&editor_affinity.text==value&&
        editor_affinity.offset==offset&&editor_affinity.trailing;
}
void remember_editor_affinity(void* widget,std::string_view value,std::size_t offset,bool trailing) {
    editor_affinity={widget,std::string(value),offset,trailing};
}
struct EditorPixelPoint { std::uint16_t x,y; };
using EditorPixelPosition=EditorPixelPoint*(*)(void*,EditorPixelPoint*);
EditorPixelPosition original_editor_caret=nullptr,original_editor_anchor=nullptr;
EditorPixelPoint* shaped_editor_position(void* widget,EditorPixelPoint* result,bool anchor) {
    const auto original=anchor?original_editor_anchor:original_editor_caret;
    try {
        const auto view=eu4unicode::native_edit_view(widget);
        const auto base=static_cast<const std::byte*>(widget);
        const auto row=*reinterpret_cast<const std::uint16_t*>(base+(anchor?0x94:0x56));
        const auto& line=view.rows.rows().at(row);
        const auto value=view.text.substr(line.start,line.length);
        if(const auto paragraph=editor_paragraph(widget,value)) {
            const auto offset=*reinterpret_cast<const std::uint16_t*>(base+(anchor?0x92:0x54));
            if(offset<=value.size()) {
                const auto font=*reinterpret_cast<const std::byte* const*>(base+0x98);
                const auto scale=*reinterpret_cast<const float*>(font+0x968);
                const auto caret=paragraph->caret(offset,!anchor&&editor_trailing(widget,view.text,line.start+offset));
                const auto x=std::round(caret.x*scale)+*reinterpret_cast<const int*>(font+0x38)+(anchor?3:0);
                const auto line_height=reinterpret_cast<int(*)(void*)>((*reinterpret_cast<void* const* const*>(font))[0x68/8])(const_cast<std::byte*>(font));
                const auto y=std::round(caret.y*scale)+*reinterpret_cast<const int*>(font+0x3c)+
                    (base[0xd9]!=std::byte{0}?row*line_height:0);
                if(std::isfinite(x)&&std::isfinite(y)&&x<=65535&&y<=65535) {
                    result->x=static_cast<std::uint16_t>((std::max)(0.f,x));
                    result->y=static_cast<std::uint16_t>((std::max)(0.f,y));
                    return result;
                }
            }
        }
    } catch(...) { log("Unicode editor caret positioning failed; native position retained."); }
    return original(widget,result);
}
EditorPixelPoint* editor_caret_position(void* widget,EditorPixelPoint* result) { return shaped_editor_position(widget,result,false); }
EditorPixelPoint* editor_anchor_position(void* widget,EditorPixelPoint* result) { return shaped_editor_position(widget,result,true); }
std::unique_ptr<eu4unicode::NativeEditorSelections> editor_selections;
eu4unicode::NativeSpriteFactory original_editor_sprite_factory=nullptr;
EditorAction original_editor_destroy=nullptr,original_editor_hide=nullptr;
using EditorSetup=void(*)(void*,void*);
EditorSetup original_editor_setup=nullptr;
void* create_editor_sprite(void* manager,const EngineString* name,void* context,unsigned char flags,EngineString* output) {
    const bool selection=reinterpret_cast<std::byte*>(_ReturnAddress())==image+0x1533a14;
    auto sprite=original_editor_sprite_factory(manager,name,context,flags,output);
    if(selection&&editor_selections) try { editor_selections->capture(sprite,manager,flags); }
        catch(...) { log("Native selection resource capture failed."); }
    return sprite;
}
void destroy_editor(void* outer) {
    forget_editor_history(static_cast<std::byte*>(outer)+0xc8);
    if(editor_selections) editor_selections->release(outer,*reinterpret_cast<void**>(static_cast<std::byte*>(outer)+0x1f8));
    original_editor_destroy(outer);
}
void hide_editor(void* outer) {
    if(editor_selections) editor_selections->hide(outer);
    original_editor_hide(outer);
}
void setup_editor(void* outer,void* value) {
    original_editor_setup(outer,value);
    if(editor_selections) editor_selections->setup(outer,value);
}
void paint_editor_selection(void* outer) noexcept {
    if(!editor_selections) return;
    try {
        const auto base=static_cast<std::byte*>(outer),widget=base+0xc8;
        const auto preview=eu4unicode::editor_preedit_preview(outer);
        if(base[0xc5]==std::byte{0}||base[0x262]!=std::byte{0}||*reinterpret_cast<std::uint64_t*>(base+0x2f0)||
           (!preview&&(widget[0x90]!=std::byte{1}||!*reinterpret_cast<std::uint64_t*>(base+0x148)))) {
            editor_selections->hide(outer);return;
        }
        const auto view=eu4unicode::native_edit_view(widget);
        if(!preview&&view.rows.rows().size()==1&&!eu4unicode::needs_paragraph_shaping(view.text)) { editor_selections->hide(outer);return; }
        const auto caret_offset=view.rows.offset({*reinterpret_cast<std::uint16_t*>(widget+0x56),*reinterpret_cast<std::uint16_t*>(widget+0x54)});
        const auto anchor_offset=preview?preview->begin:view.rows.offset({*reinterpret_cast<std::uint16_t*>(widget+0x94),*reinterpret_cast<std::uint16_t*>(widget+0x92)});
        const auto begin=preview?preview->begin:(std::min)(caret_offset,anchor_offset);
        const auto end=preview?preview->end:(std::max)(caret_offset,anchor_offset);
        const auto font=*reinterpret_cast<std::byte**>(widget+0x98);
        const auto sprite=*reinterpret_cast<void**>(base+0x1f0);
        if(!sprite) { editor_selections->hide(outer);return; }
        const auto scale=*reinterpret_cast<float*>(font+0x968);
        if(!std::isfinite(scale)||scale<=0) { editor_selections->hide(outer);return; }
        auto origin=eu4unicode::native_sprite_input_position(sprite);
        if(!std::isfinite(origin.x)||!std::isfinite(origin.y)) { editor_selections->hide(outer);return; }
        EditorPixelPoint pixel{};shaped_editor_position(widget,&pixel,false);
        origin.x-=pixel.x;origin.y-=pixel.y;
        const auto line_height=reinterpret_cast<int(*)(void*)>((*reinterpret_cast<void***>(font))[0x68/8])(font);
        const auto height=line_height/2;
        std::vector<eu4unicode::NativeSelectionRect> boxes;
        for(std::size_t index=0;index<view.rows.rows().size();++index) {
            const auto& row=view.rows.rows()[index];
            const auto first=(std::max)(begin,row.start),last=(std::min)(end,row.start+row.length);
            if(first>=last) continue;
            const auto text=view.text.substr(row.start,row.length);
            std::vector<eu4unicode::SelectionRegion> regions;
            if(const auto paragraph=editor_paragraph(widget,text)) regions=paragraph->selection(first-row.start,last-row.start);
            else {
                const auto measure=native_editor_measure(widget,text);
                const auto x=measure(first-row.start),finish=measure(last-row.start);
                regions.push_back({first,last-first,static_cast<float>(x)/scale,0,static_cast<float>(finish-x)/scale,static_cast<float>(line_height)/scale,0});
            }
            const auto top=widget[0xd9]!=std::byte{0}?index*line_height:0;
            for(const auto& region:regions) boxes.push_back({
                static_cast<int>(std::round(origin.x+region.x*scale))+*reinterpret_cast<int*>(font+0x38)+(preview?0:3),
                static_cast<int>(std::round(origin.y+region.y*scale+top))+*reinterpret_cast<int*>(font+0x3c)+(preview?line_height-2:height),
                static_cast<int>(std::ceil(region.width*scale))+(preview?0:2),preview?1:height+2});
        }
        editor_selections->update(outer,*reinterpret_cast<void**>(base+0x1f8),*reinterpret_cast<void**>(base+0x298),boxes);
    } catch(...) { editor_selections->hide(outer);log("Native shaped selection update failed."); }
}
eu4unicode::PrefixMeasure native_editor_measure(void* widget,std::string_view text) {
    const auto base=static_cast<const std::byte*>(widget);
    const auto font=*reinterpret_cast<void* const*>(base+0x98);
    const auto flags=*reinterpret_cast<const unsigned char*>(base+0xe4);
    using Measure=int(*)(void*,const char*,int,unsigned char);
    const auto measure=reinterpret_cast<Measure>((*static_cast<void***>(font))[0x60/8]);
    return [font,flags,measure,text](std::size_t length) {
        const std::string prefix(text.substr(0,length));
        return measure(font,prefix.c_str(),static_cast<int>(length),flags);
    };
}
void editor_point(void* widget,const EngineString* row,const EditorPoint* point) {
    if(point->x<0||point->y<0) {
        original_editor_point(widget,row,point); return;
    }
    try {
        const auto view=eu4unicode::native_edit_view(widget);
        const auto base=static_cast<std::byte*>(widget);
        const auto font=*reinterpret_cast<const std::byte* const*>(base+0x98);
        const auto height=reinterpret_cast<int(*)(void*)>((*reinterpret_cast<void* const* const*>(font))[0x68/8])(const_cast<std::byte*>(font));
        if(height<=0) { original_editor_point(widget,row,point);return; }
        const auto index=base[0xd9]!=std::byte{0}?(std::min)(static_cast<std::size_t>(point->y/height),view.rows.rows().size()-1):0;
        const auto& line=view.rows.rows()[index];const auto value=view.text.substr(line.start,line.length);
        std::size_t hit=0;bool trailing=false;
        if(const auto paragraph=editor_paragraph(widget,value)) {
            const auto scale=*reinterpret_cast<const float*>(font+0x968);
            const auto position=paragraph->hit_test(point->x/scale,0);
            hit=position.byte_offset;trailing=position.trailing;
        } else hit=eu4unicode::nearest_grapheme_boundary(value,point->x,native_editor_measure(widget,value));
        *reinterpret_cast<std::uint16_t*>(base+0x54)=static_cast<std::uint16_t>(hit);
        *reinterpret_cast<std::uint16_t*>(base+0x56)=static_cast<std::uint16_t>(index);
        remember_editor_affinity(widget,view.text,line.start+hit,trailing);
    } catch(...) { log("Unicode editor hit testing failed; native hit testing retained.");original_editor_point(widget,row,point); }
}
int editor_width_fit(void* widget,const EngineString* text) {
    const auto value=std::string_view(text->data(),static_cast<std::size_t>(text->size));
    if(text->size>32000||!eu4unicode::valid_utf8(value)) return original_editor_width_fit(widget,text);
    auto line_end=value.find('\n');
    if(line_end!=std::string_view::npos&&line_end&&value[line_end-1]=='\r') --line_end;
    const auto line=value.substr(0,line_end);
    try {
        if(line.empty()) return 0;
        const auto base=static_cast<const std::byte*>(widget);
        const auto font=*reinterpret_cast<void* const*>(base+0x98);
        using Margin=int(*)(void*,unsigned char);
        const auto margin=reinterpret_cast<Margin>((*static_cast<void***>(font))[0xa8/8])(font,1);
        const auto width=static_cast<int>(*reinterpret_cast<const std::uint16_t*>(base+0x68))-margin;
        const auto fit=eu4unicode::fitting_grapheme_prefix(line,width,native_editor_measure(widget,line));
        // Consume an oversized first grapheme so native row construction makes
        // progress without splitting it or inserting unbounded empty rows.
        return static_cast<int>(fit?fit:eu4unicode::grapheme_boundaries(line)[1]);
    } catch(...) { log("Unicode editor width fitting failed; row kept whole."); return static_cast<int>(line.size()); }
}
int editor_word_break(void* widget,const EngineString* text,int fit) {
    const auto result=original_editor_word_break(widget,text,fit);
    const auto value=std::string_view(text->data(),static_cast<std::size_t>(text->size));
    if(result<0||text->size>32000||!eu4unicode::valid_utf8(value)) return result;
    try {
        return static_cast<int>(eu4unicode::grapheme_prefix(value,static_cast<std::size_t>(result)+1))-1;
    } catch(...) { log("Unicode editor word boundary failed; word break excluded."); return -1; }
}
void editor_arrow(void* widget,bool right) {
    const auto original=right?original_editor_right:original_editor_left;
    try {
            const auto view=eu4unicode::native_edit_view(widget);
            auto base=static_cast<std::byte*>(widget);
            auto column=reinterpret_cast<std::uint16_t*>(base+0x54);
            const auto index=*reinterpret_cast<std::uint16_t*>(base+0x56);
            const auto& row=view.rows.rows().at(index);
            const auto value=view.text.substr(row.start,row.length);
            const auto offset=view.rows.offset({index,*column});
            if(const auto paragraph=editor_paragraph(widget,value)) {
                const auto target=paragraph->move_caret(*column,editor_trailing(widget,view.text,offset),right);
                if(target.byte_offset==*column&&target.trailing==editor_trailing(widget,view.text,offset)&&view.rows.rows().size()>1&&
                   ((right&&index+1<view.rows.rows().size())||(!right&&index))) {
                    const auto next=right?index+1:index-1;const auto& line=view.rows.rows()[next];
                    const auto content=view.text.substr(line.start,line.length);std::size_t hit=right?0:line.length;bool trailing=false;
                    if(const auto layout=editor_paragraph(widget,content)) { const auto stop=layout->hit_test(right?-1.f:32767.f,0);hit=stop.byte_offset;trailing=stop.trailing; }
                    eu4unicode::native_edit_caret(widget,line.start+hit);
                    *reinterpret_cast<std::uint16_t*>(base+0x56)=static_cast<std::uint16_t>(next);*column=static_cast<std::uint16_t>(hit);
                    remember_editor_affinity(widget,view.text,line.start+hit,trailing);
                } else {
                    eu4unicode::native_edit_caret(widget,row.start+target.byte_offset);
                    *reinterpret_cast<std::uint16_t*>(base+0x56)=index;*column=static_cast<std::uint16_t>(target.byte_offset);
                    remember_editor_affinity(widget,view.text,row.start+target.byte_offset,target.trailing);
                }
                reinterpret_cast<void(*)(void*)>((*static_cast<void***>(widget))[0x208/8])(widget);
                return;
            }
            const auto target=eu4unicode::plan_edit(view.text,offset,
                right?eu4unicode::EditKey::right:eu4unicode::EditKey::left).caret;
            eu4unicode::native_edit_caret(widget,target);
            reinterpret_cast<void(*)(void*)>((*static_cast<void***>(widget))[0x208/8])(widget);
    } catch(...) { log("Unicode editor movement failed; native action retained.");original(widget); }
}
void editor_left(void* widget) { editor_arrow(widget,false); }
void editor_right(void* widget) { editor_arrow(widget,true); }
void editor_selection(void* widget) {
    auto base=static_cast<std::byte*>(widget);
    try {
        const auto view=eu4unicode::native_edit_view(widget);
        const auto anchor=view.rows.offset({*reinterpret_cast<std::uint16_t*>(base+0x94),*reinterpret_cast<std::uint16_t*>(base+0x92)});
        const auto caret=view.rows.offset({*reinterpret_cast<std::uint16_t*>(base+0x56),*reinterpret_cast<std::uint16_t*>(base+0x54)});
        const auto selection=eu4unicode::align_selection(view.text,anchor,caret);
        const auto first=view.rows.position(selection.anchor),last=view.rows.position(selection.caret);
        *reinterpret_cast<std::uint16_t*>(base+0x92)=static_cast<std::uint16_t>(first.column);
        *reinterpret_cast<std::uint16_t*>(base+0x94)=static_cast<std::uint16_t>(first.row);
        *reinterpret_cast<std::uint16_t*>(base+0x54)=static_cast<std::uint16_t>(last.column);
        *reinterpret_cast<std::uint16_t*>(base+0x56)=static_cast<std::uint16_t>(last.row);
        *reinterpret_cast<std::uint32_t*>(base+0x50)=static_cast<std::uint32_t>(last.column);
    } catch(...) { log("Unicode editor selection alignment failed; native selection retained."); }
    original_editor_selection(widget);
}
struct EditorHistoryEntry {
    eu4unicode::EditHistory history;
    eu4unicode::EditState before;
    unsigned depth=0;
    bool alive=true;
};
thread_local std::unordered_map<void*,std::shared_ptr<EditorHistoryEntry>> editor_histories;
void forget_editor_history(void* widget) noexcept {
    const auto found=editor_histories.find(widget);
    if(found!=editor_histories.end()) { found->second->alive=false;editor_histories.erase(found); }
    if(editor_affinity.owner==widget) clear_editor_affinity();
}
struct EditorTransaction {
    void* widget;std::shared_ptr<EditorHistoryEntry> entry;
    explicit EditorTransaction(void* owner):widget(owner) {
        try {
            auto& stored=editor_histories[owner];if(!stored) stored=std::make_shared<EditorHistoryEntry>();
            entry=stored;
            if(!entry->depth) entry->before=eu4unicode::native_edit_state(widget);
            ++entry->depth;
        } catch(...) { entry.reset(); }
    }
    ~EditorTransaction() {
        if(!entry||--entry->depth||!entry->alive) return;
        try { entry->history.record(entry->before,eu4unicode::native_edit_state(widget)); }
        catch(...) { entry->history.clear(); }
    }
};
void editor_vertical(void* widget,bool down,bool extend) {
    const auto view=eu4unicode::native_edit_view(widget);
    auto base=static_cast<std::byte*>(widget);
    const auto row=*reinterpret_cast<std::uint16_t*>(base+0x56);
    if((down&&row+1>=view.rows.rows().size())||(!down&&!row)) return;
    EditorPixelPoint point{};shaped_editor_position(widget,&point,false);
    const auto font=*reinterpret_cast<const std::byte* const*>(base+0x98);
    const auto x=(std::max)(0,static_cast<int>(point.x)-*reinterpret_cast<const int*>(font+0x38));
    const auto next=down?row+1:row-1;const auto& line=view.rows.rows()[next];
    const auto value=view.text.substr(line.start,line.length);
    std::size_t hit=0;bool trailing=false;
    if(const auto paragraph=editor_paragraph(widget,value)) {
        const auto target=paragraph->hit_test(x/(*reinterpret_cast<const float*>(font+0x968)),0);
        hit=target.byte_offset;trailing=target.trailing;
    } else hit=eu4unicode::nearest_grapheme_boundary(value,x,native_editor_measure(widget,value));
    if(extend&&base[0x90]==std::byte{0}) *reinterpret_cast<std::uint32_t*>(base+0x92)=*reinterpret_cast<std::uint32_t*>(base+0x54);
    eu4unicode::native_edit_caret(widget,line.start+hit,!extend);
    *reinterpret_cast<std::uint16_t*>(base+0x56)=static_cast<std::uint16_t>(next);
    *reinterpret_cast<std::uint16_t*>(base+0x54)=static_cast<std::uint16_t>(hit);
    if(extend) { editor_selection(widget);base[0x90]=std::byte{1}; }
    remember_editor_affinity(widget,view.text,line.start+hit,trailing);
    reinterpret_cast<void(*)(void*)>((*static_cast<void***>(widget))[0x208/8])(widget);
}
bool editor_key(void* widget,const KeyEvent* event) {
    try {
        const auto composition=eu4unicode::native_composition();
        if(composition.active&&(event->key==8||event->key==127||event->key==13||
           (event->key>=0x4000004a&&event->key<=0x40000052))) return true;
        if((event->modifiers==1&&(event->key=='z'||event->key=='y'))||(event->modifiers==5&&event->key=='z')) {
            if(composition.active) return true;
            const auto found=editor_histories.find(widget);
            if(found!=editor_histories.end()) {
                const auto state=eu4unicode::native_edit_state(widget);
                const auto target=event->key=='z'&&event->modifiers==1?found->second->history.undo(state):found->second->history.redo(state);
                if(target) { eu4unicode::native_edit_restore(widget,*target);clear_editor_affinity(); }
            }
            return true;
        }
        EditorTransaction transaction(widget);
        const auto state=eu4unicode::native_edit_state(widget);
        const auto base=static_cast<std::byte*>(widget);
        if(base[0x90]!=std::byte{0}) editor_selection(widget);
        if(event->modifiers==4&&(event->key==0x40000050||event->key==0x4000004f)) {
            const auto vtable=*static_cast<void***>(widget);
            reinterpret_cast<EditorAction>(vtable[(event->key==0x40000050?0xd0:0xe0)/8])(widget);
            return true;
        }
        if((event->modifiers==0||event->modifiers==4)&&base[0xd9]!=std::byte{0}&&
           (event->key==0x40000051||event->key==0x40000052)) {
            editor_vertical(widget,event->key==0x40000051,event->modifiers==4);return true;
        }
        if(!event->modifiers&&(event->key==8||event->key==127)) {
            eu4unicode::EditResult result;
            if(state.selection.anchor!=state.selection.caret)
                result=eu4unicode::replace_selection(state.text,state.selection.anchor,state.selection.caret,"",32000);
            else {
                const auto plan=eu4unicode::plan_edit(state.text,state.selection.caret,event->key==8?
                    eu4unicode::EditKey::backspace:eu4unicode::EditKey::forward_delete);
                result={state.text,plan.caret};result.text.erase(plan.erase_begin,plan.erase_end-plan.erase_begin);
            }
            if(result.text!=state.text) eu4unicode::native_edit_restore(widget,{std::move(result.text),{result.caret,result.caret}});
            return true;
        }
        return original_editor_key(widget,event);
    } catch(...) { log("Unicode editor key handling failed; native action retained.");return original_editor_key(widget,event); }
}
using EditorCharacter=bool(*)(void*,const char*);
using EditorInsert=void(*)(void*,const EngineString*);
EditorCharacter original_editor_character=nullptr;
EditorInsert original_editor_insert=nullptr;
struct ActiveCommit { void* widget; const EngineString* text; };
thread_local ActiveCommit active_commit{};
void insert_editor_commit(void* widget,const EngineString* text) {
    EditorTransaction transaction(widget);
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-
        reinterpret_cast<std::uintptr_t>(image);
    if(caller==0x1535615&&active_commit.widget==widget&&active_commit.text)
        text=active_commit.text;
    if(static_cast<std::byte*>(widget)[0x90]!=std::byte{0}) editor_selection(widget);
    original_editor_insert(widget,text);
    if(transaction.entry&&!transaction.entry->alive) return;
    try {
        const auto view=eu4unicode::native_edit_view(widget);
        const auto base=static_cast<std::byte*>(widget);
        const auto caret=view.rows.offset({*reinterpret_cast<std::uint16_t*>(base+0x56),*reinterpret_cast<std::uint16_t*>(base+0x54)});
        const auto boundaries=eu4unicode::grapheme_boundaries(view.text);
        const auto aligned=*std::lower_bound(boundaries.begin(),boundaries.end(),caret);
        if(aligned!=caret) eu4unicode::native_edit_caret(widget,aligned);
    } catch(...) { log("Unicode commit caret alignment failed."); }
}
void paste_editor_clipboard(void* widget) {
    using ClipboardText=char*(*)();
    using ClipboardFree=void(*)(void*);
    const auto release=reinterpret_cast<ClipboardFree>(image+0x1735e00);
    std::unique_ptr<char,ClipboardFree> clipboard(
        reinterpret_cast<ClipboardText>(image+0x1734490)(),release);
    if(!clipboard) return;
    const auto length=strnlen_s(clipboard.get(),32001);
    if(!length||length>32000) return;
    const std::string_view value(clipboard.get(),length);
    if(!eu4unicode::valid_utf8(value)) return;
    try {
        EngineString text{};text.capacity=15;
        struct DestroyText {
            EngineString* text;
            ~DestroyText(){reinterpret_cast<void(*)(EngineString*)>(image+0x95660)(text);}
        } destroy{&text};
        original_assign_text(&text,value.data(),value.size());
        const auto base=static_cast<std::byte*>(widget);
        const auto font=*reinterpret_cast<void**>(base+0x98);
        if(font&&base[0xe4]!=std::byte{0}) {
            const auto previous=clipboard_font_filter;
            struct Restore { bool previous; ~Restore(){clipboard_font_filter=previous;} } restore{previous};
            clipboard_font_filter=true;
            using Transform=void(*)(void*,EngineString*);
            reinterpret_cast<Transform>((*static_cast<void***>(font))[0xc0/8])(font,&text);
        }
        const auto blacklist=reinterpret_cast<const EngineString*>(base+0xb8);
        const auto filtered=eu4unicode::filter_editor_characters(
            {text.data(),static_cast<std::size_t>(text.size)},
            {blacklist->data(),static_cast<std::size_t>(blacklist->size)});
        if(filtered.empty()) return;
        original_assign_text(&text,filtered.data(),filtered.size());
        // Keep the active selection until the existing insert action replaces
        // it. Rejected clipboard text leaves text, caret and selection intact.
        insert_editor_commit(widget,&text);
    } catch(...) { log("Unicode clipboard paste failed; insertion excluded."); }
}
bool consume_editor_commit(void* callback,const char* character) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress())-
        reinterpret_cast<std::uintptr_t>(image);
    // This native caller passes the type-2 queue node's text member, not an
    // arbitrary one-byte character pointer. Other callers retain that ABI.
    if(caller!=0x14e9cb6) return original_editor_character(callback,character);
    const auto event=reinterpret_cast<const eu4unicode::NativeTextEvent*>(character-0x10);
    if(event->utf8_tag!=eu4unicode::native_utf8_tag)
        return original_editor_character(callback,character);
    const auto payload=eu4unicode::queued_utf8(*event);
    if(payload.empty()) return false;
    try {
        auto base=static_cast<std::byte*>(callback);
        const auto blacklist=reinterpret_cast<const EngineString*>(base+0xd0);
        auto filtered=eu4unicode::filter_editor_characters(payload,
            {blacklist->data(),static_cast<std::size_t>(blacklist->size)});
        const auto editor_blacklist=reinterpret_cast<const EngineString*>(base-0x50);
        filtered=eu4unicode::filter_editor_characters(filtered,
            {editor_blacklist->data(),static_cast<std::size_t>(editor_blacklist->size)});
        // Preserve the original context-dependent seven-character restriction.
        using FindElement=void*(*)(void*,const void*);
        const auto find=reinterpret_cast<FindElement>(image+0x14db3c0);
        if(find(*reinterpret_cast<void**>(base+0x10),base+0x30)&&
           *reinterpret_cast<const unsigned char*>(base-0x24))
            filtered=eu4unicode::filter_editor_characters(filtered,
                {reinterpret_cast<const char*>(image+0x1d91328),7});
        if(filtered.empty()) return false;
        EngineString text{};
        text.size=filtered.size();
        text.capacity=filtered.size()<16?15:filtered.size();
        if(text.capacity<16) std::memcpy(text.storage.inline_bytes,filtered.c_str(),filtered.size()+1);
        else text.storage.pointer=filtered.c_str();
        const auto previous=active_commit;
        struct Restore { ActiveCommit previous; ~Restore(){active_commit=previous;} } restore{previous};
        active_commit={base-0x108,&text};
        // The native character consumer keeps its notification and focus logic;
        // its single insertion call receives the complete borrowed string.
        return original_editor_character(callback,filtered.c_str());
    } catch(...) { log("Unicode queued commit failed; insertion excluded."); return false; }
}
extern "C" void trim_editor_grapheme(void* widget) {
    auto base=static_cast<std::byte*>(widget);
    auto text=reinterpret_cast<EngineString*>(base+0x30);
    const auto value=std::string_view(text->data(),static_cast<std::size_t>(text->size));
    if(eu4unicode::valid_utf8(value)&&!value.empty()) {
        using Position=std::uint32_t(*)(void*,std::uint32_t,std::uint32_t);
        const auto position=reinterpret_cast<Position>(image+0x153aa90)(widget,
            *reinterpret_cast<std::uint16_t*>(base+0x56),*reinterpret_cast<std::uint16_t*>(base+0x54));
        if(position&&position<=value.size()) {
            try {
                const auto plan=eu4unicode::plan_edit(value,position,eu4unicode::EditKey::backspace);
                auto data=const_cast<char*>(text->data());
                std::memmove(data+plan.erase_begin,data+plan.erase_end,
                    value.size()-plan.erase_end+1);
                text->size-=plan.erase_end-plan.erase_begin;
                base[0x100]=base[0x101]=std::byte{1};
                using MoveCaret=void(*)(void*,std::uint32_t);
                reinterpret_cast<MoveCaret>(image+0x1536f50)(widget,static_cast<std::uint32_t>(plan.caret));
                struct Rows { const std::byte* begin; const std::byte* end; };
                using GetRows=const Rows*(*)(void*);
                const auto vtable=*static_cast<void***>(widget);
                const auto rows=reinterpret_cast<GetRows>(vtable[0x1a0/8])(widget);
                *reinterpret_cast<std::uint16_t*>(base+0x60)=
                    static_cast<std::uint16_t>((rows->end-rows->begin)/40);
                return;
            } catch(...) { log("Unicode editor fitting failed."); }
        }
    }
    // Keep the observed native behavior for non-UTF-8 text and empty positions.
    using Backspace=void(*)(void*);
    const auto vtable=*static_cast<void***>(widget);
    reinterpret_cast<Backspace>(vtable[0x138/8])(widget);
}
void import_text(const EngineString* key,const EngineString* value,int version,
                 int /*unused*/,const LoadContext* context) {
    const auto text=std::string_view(value->data(),static_cast<std::size_t>(value->size));
    if(!eu4unicode::valid_utf8(text)) {
        log("Rejected invalid UTF-8 localization value.");
        return;
    }
    register_text(context->collection,key->data(),value->data(),context->line,version,context->replace);
    if(std::strcmp(key->data(),"FE_SINGLE_PLAYER")==0) {
        log("Registered FE_SINGLE_PLAYER without single-byte conversion:");
        log(value->data());
    }
}

bool hash_matches(const std::filesystem::path& file) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) return false;
    DWORD object_size=0,returned=0;
    BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&object_size),sizeof(object_size),&returned,0);
    std::vector<UCHAR> object(object_size);
    bool good=BCryptCreateHash(algorithm,&hash,object.data(),object_size,nullptr,0,0)>=0;
    std::ifstream stream(file,std::ios::binary);
    std::array<char,65536> buffer{};
    if(!stream) good=false;
    while(good && stream) {
        stream.read(buffer.data(),buffer.size());
        good=BCryptHashData(hash,reinterpret_cast<PUCHAR>(buffer.data()),static_cast<ULONG>(stream.gcount()),0)>=0;
    }
    std::array<UCHAR,32> digest{};
    if(good) good=BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm,0);
    constexpr UCHAR expected[]={0x9a,0xd3,0xef,0xe1,0xaf,0x16,0x9f,0x40,0xee,0x57,0x7f,0x9d,0xae,0x5d,0xeb,0xbc,
        0x87,0xaf,0x6f,0xb8,0xb5,0x45,0x0f,0xb3,0x45,0xeb,0xf1,0x10,0xdc,0x4d,0x77,0x1a};
    return good && std::memcmp(digest.data(),expected,32)==0;
}
struct Site { std::size_t rva; const char* expected; };
std::vector<std::byte> bytes(const char* hex) {
    std::vector<std::byte> result;
    while(*hex) { char pair[]={hex[0],hex[1],0}; result.push_back(static_cast<std::byte>(std::strtoul(pair,nullptr,16))); hex+=2; }
    return result;
}
bool check(const Site& site) {
    const auto expected=bytes(site.expected);
    if(std::memcmp(image+site.rva,expected.data(),expected.size())==0) return true;
    char message[100]; std::snprintf(message,sizeof(message),"Instruction mismatch at RVA %zx; patch refused.",site.rva); log(message);
    return false;
}
bool write(std::size_t rva,const void* data,std::size_t size) {
    DWORD previous=0,unused=0;
    if(!VirtualProtect(image+rva,size,PAGE_EXECUTE_READWRITE,&previous)) return false;
    std::memcpy(image+rva,data,size);
    FlushInstructionCache(GetCurrentProcess(),image+rva,size);
    return VirtualProtect(image+rva,size,previous,&unused)!=0;
}
}

extern "C" {
std::uintptr_t g_main_draw_return,g_main_copy_return,g_main_measure_return;
std::uintptr_t g_ui_vertices,g_main_page_return,g_button_page_return;
std::uintptr_t g_main_geometry_entry_return,g_main_geometry_end_return;
std::uintptr_t g_button_geometry_entry_return,g_button_geometry_end_return;
std::uintptr_t g_bitmap_measure_return,g_bitmap_split_return,g_copy_buffer;
std::uintptr_t g_bitmap_advance_return,g_list_measure_return,g_list_advance_return;
std::uintptr_t g_split_format_return,g_split_plain_entry,g_list_format_return,g_list_plain_entry;
std::uintptr_t g_alternate_format_return,g_alternate_plain_entry,g_alternate_end,g_alternate_advance_return;
std::uintptr_t g_split_kern_return,g_list_kern_return,g_alternate_kern_return;
std::uintptr_t g_button_wrap_return,g_button_wrap_branch;
std::uintptr_t g_popup_entry_return,g_popup_end_return,g_popup_data;
std::uintptr_t g_popup_copy_return,g_popup_color_copy_return,g_popup_icon_copy_return;
std::uintptr_t g_popup_format_return,g_popup_plain_entry,g_popup_measure_return;
std::uintptr_t g_popup_wrap_return,g_popup_advance_entry,g_popup_advance_return;
std::uintptr_t g_popup_draw_format_return,g_popup_draw_plain_entry,g_popup_draw_return;
std::uintptr_t g_popup_icon_end_return,g_popup_measure_kern_return,g_popup_draw_kern_return,g_popup_page_return;
std::uintptr_t g_heap_pointer,g_heap_alloc,g_heap_return;
std::uintptr_t g_button_copy_return,g_button_measure_return,g_button_draw_return,g_button_loop,g_button_end;
std::uintptr_t g_alternate_measure_return,g_wrap_return,g_wrap_branch;
std::uintptr_t g_main_measure_entry,g_main_format_return,g_main_plain_entry;
std::uintptr_t g_main_icon_copy_return,g_main_icon_draw_return;
std::uintptr_t g_button_format_return,g_button_plain_entry,g_button_draw_format_return,g_button_draw_plain_entry;
std::uintptr_t g_button_icon_copy_return,g_button_icon_draw_return;
std::uintptr_t g_bitmap_format_return,g_bitmap_plain_entry,g_bitmap_icon_end_return;
std::uintptr_t g_map_copy_return,g_map_measure_return,g_map_draw_return,g_map_kern_return;
std::uintptr_t g_map_justify_draw_return,g_map_justify_measure_return,g_map_justify_advance_return;
std::uintptr_t g_map_justify_count_return,g_map_justify_single_return;
std::uintptr_t g_map_adjust_copy_return,g_map_adjust_glyph_return,g_map_upper_return,g_map_lower_return;
std::uintptr_t g_map_vertex_count_return;
std::uintptr_t g_map_page_tag_return,g_map_justify_page_tag_return;
std::uintptr_t g_map_kern_call;
std::uintptr_t g_map_fit_format_return,g_map_fit_plain_entry,g_map_fit_measure_return,g_map_fit_kern_return;
std::uintptr_t g_map_fit_icon_end_return,g_map_adjust_gap_end_return,g_map_adjust_last_return;
std::uintptr_t g_country_shape_return,g_province_shape_return,g_country_gap_return,g_country_gap_skip;
std::uintptr_t g_input_return;
std::uintptr_t g_editor_fit_return;
std::uintptr_t g_text_limit_return;
std::uintptr_t g_font_allocate,g_font_duplicate,g_font_store_return,g_font_initialize,g_font_skip,g_engine_new;
std::uintptr_t g_path_pair_return;
std::uintptr_t g_wide_compare_left_return,g_wide_compare_right_return;
void main_draw_hook(); void main_copy_hook(); void main_measure_hook();
void main_page_hook();void button_page_hook();
void main_geometry_entry_hook();void main_geometry_end_hook();
void button_geometry_entry_hook();void button_geometry_end_hook();
void bitmap_measure_hook(); void bitmap_split_hook();
void bitmap_advance_hook();void list_measure_hook();void list_advance_hook();
void split_format_hook();void list_format_hook();void alternate_format_hook();void alternate_advance_hook();
void split_kern_hook();void list_kern_hook();void alternate_kern_hook();
void button_wrap_hook();
void popup_entry_hook();void popup_end_hook();void popup_copy_hook();void popup_color_copy_hook();void popup_icon_copy_hook();
void popup_format_hook();void popup_measure_hook();void popup_wrap_hook();void popup_advance_hook();
void popup_draw_format_hook();void popup_draw_hook();void popup_icon_end_hook();
void popup_measure_kern_hook();void popup_draw_kern_hook();void popup_page_hook();
void heap_zero_hook();
void button_copy_hook(); void button_measure_hook(); void button_draw_hook(); void button_advance_hook();
void alternate_measure_hook(); void main_wrap_hook();
void main_format_hook(); void main_icon_copy_hook(); void main_icon_draw_hook();
void button_format_hook(); void button_draw_format_hook();
void button_icon_copy_hook(); void button_icon_draw_hook();
void bitmap_format_hook(); void bitmap_icon_end_hook();
void map_copy_hook(); void map_measure_hook(); void map_draw_hook(); void map_kern_hook();
void map_justify_draw_hook(); void map_justify_measure_hook(); void map_justify_advance_hook();
void map_justify_count_hook();
void map_adjust_copy_hook(); void map_adjust_glyph_hook(); void map_upper_hook(); void map_lower_hook();
void map_vertex_count_hook();
void map_page_tag_hook();void map_justify_page_tag_hook();
void map_fit_format_hook();void map_fit_measure_hook();void map_fit_kern_hook();void map_fit_icon_end_hook();
void map_adjust_gap_end_hook();void map_adjust_last_hook();
void country_shape_hook();void province_shape_hook();void country_shape_gap_hook();
void input_hook();
void editor_fit_hook();
void text_limit_hook();
void font_lookup_hook(); void font_store_hook();
void path_pair_hook();
void wide_compare_left_hook(); void wide_compare_right_hook();
void font_allocate_hook();
void* allocate_unicode_glyph(void* const* table,std::uint32_t scalar) noexcept {
    auto record=eu4unicode::allocate_unicode_glyph(table,scalar);
    if(!record) log("Unicode glyph allocation failed; glyph skipped.");
    return record;
}
void* find_supplementary_glyph(void* const* table,std::uint32_t scalar) noexcept {
    if(auto shaped=eu4unicode::find_paragraph_glyph(table,scalar)) return shaped;
    if(scalar<=255&&table[scalar]) return table[scalar];
    auto glyph=eu4unicode::find_unicode_glyph(table,scalar);
    return glyph?glyph:eu4unicode::find_dynamic_glyph(table,scalar);
}
void mark_map_font_glyph(const eu4unicode::NativeGlyph* glyph,eu4unicode::MapFontVertex* vertices) noexcept {
    eu4unicode::mark_map_font_glyph(glyph,vertices);
}
void remember_map_font_glyph(const eu4unicode::NativeGlyph* glyph) noexcept { eu4unicode::remember_map_font_glyph(glyph); }
void mark_current_map_font_glyph(eu4unicode::MapFontVertex* vertices) noexcept { eu4unicode::mark_current_map_font_glyph(vertices); }
void* find_loaded_glyph(void* const* table,std::uint32_t scalar) noexcept {
    return scalar<=0xff?table[scalar]:eu4unicode::find_unicode_glyph(table,scalar);
}
void store_loaded_glyph(void** table,std::uint32_t scalar,void* glyph) noexcept {
    if(scalar<=0xff) {
        table[scalar]=glyph;
        if(scalar==0x41&&!eu4unicode::bind_unicode_font(table))
            log("Unicode font alias binding failed.");
    }
}
std::size_t bounded_text_length(const char* source,std::size_t length) noexcept {
    return eu4unicode::scalar_prefix({source,length},32000);
}
void mark_popup_font_glyph(const eu4unicode::NativeGlyph* glyph,eu4unicode::PopupFontVertex* vertices) noexcept { eu4unicode::mark_popup_font_glyph(glyph,vertices); }
void begin_popup_font(void* font) { eu4unicode::begin_popup_font(font); }
void end_popup_font() noexcept { eu4unicode::end_popup_font(); }
const EngineString* begin_main_paragraph(void* font,const EngineString* source,const int* box,const std::byte* arguments) noexcept {
    const auto inset=*reinterpret_cast<const int*>(arguments);
    const auto formatted=arguments[0x18]!=std::byte{0};
    return eu4unicode::begin_native_paragraph(font,source,box,inset,formatted);
}
const EngineString* begin_button_paragraph(void* font,const EngineString* source,const std::byte* arguments) noexcept {
    return eu4unicode::begin_native_button_paragraph_arguments(font,source,arguments);
}
const EngineString* begin_popup_paragraph(void* font,const EngineString* source,int width) noexcept {
    return eu4unicode::begin_native_popup_paragraph(font,source,width);
}
void end_main_paragraph() noexcept { eu4unicode::end_native_paragraph(); }
void shape_country_map_text(void* font,EngineString* source) noexcept {
    const auto draw=eu4unicode::prepare_native_map_paragraph(font,source);
    if(draw!=source) reinterpret_cast<eu4unicode::NativeStringAssignment>(image+0x95110)(source,draw->data(),draw->size);
}
const void* shape_province_map_text(void* font,const void* source) noexcept { return eu4unicode::prepare_native_map_label(font,source); }
bool map_paragraph_active() noexcept { return eu4unicode::native_map_paragraph_active(); }
std::size_t next_layout_scalar(const EngineString* source,std::size_t offset) noexcept {
    return eu4unicode::native_scalar_next({source->data(),static_cast<std::size_t>(source->size)},offset);
}
std::size_t next_layout_offset(const char* source,std::size_t length,std::size_t offset) noexcept {
    return eu4unicode::native_scalar_next({source,length},offset);
}
std::uint64_t decode_layout_range(const char* source,std::size_t length) noexcept {
    const auto scalar=eu4unicode::native_measure_scalar({source,length});
    if(!scalar.bytes) return UINT64_MAX;
    return scalar.value|(static_cast<std::uint64_t>(scalar.bytes-1)<<32);
}
std::uint64_t format_layout_range(const char* source,std::size_t length) noexcept {
    const auto scalar=eu4unicode::native_measure_scalar({source,length});
    if(!scalar.bytes) return UINT64_MAX;
    if(scalar.value==0xa7&&length<=scalar.bytes) return UINT64_MAX;
    if(scalar.value==0xa3&&eu4unicode::native_text_unit({source,length},0).kind!=eu4unicode::TextUnitKind::icon)
        return UINT64_MAX;
    // These fixed-size ASCII tokens are specific to this native width routine.
    if((scalar.value=='@'&&length<4)||(scalar.value=='{'&&length<3)) return UINT64_MAX;
    return scalar.value|((scalar.value==0xa7||scalar.value==0xa3||scalar.value==0xa4)?
        static_cast<std::uint64_t>(scalar.bytes-1)<<32:0);
}
std::uint64_t decode_z(const char* text) noexcept {
    std::size_t length=0;
    while(length<4 && text[length]) ++length;
    const auto scalar=eu4unicode::decode({text,length});
    // Engine-owned compiled literals still include Windows-1252 bytes. This
    // adapter is confined to glyph lookup; localization input remains strict.
    const auto slot=scalar.valid?eu4unicode::bitmap_slot(scalar.value):static_cast<unsigned char>(*text);
    const auto consumed=scalar.bytes?scalar.bytes:1;
    return slot | (static_cast<std::uint64_t>(consumed-1)<<32);
}
std::uint64_t copy_scalar(const char* source,std::size_t remaining,char* destination,std::size_t available) noexcept {
    auto scalar=eu4unicode::decode({source,remaining});
    const auto consumed=scalar.bytes?scalar.bytes:1;
    last_scalar_bytes=static_cast<std::uint32_t>(consumed);
    last_slot=scalar.valid?eu4unicode::bitmap_slot(scalar.value):static_cast<unsigned char>(*source);
    if(consumed<=available) std::memcpy(destination,source,consumed);
    return last_slot | (static_cast<std::uint64_t>(consumed-1)<<32);
}
void prepare_wrap_context(const char* source,std::size_t length) noexcept {
    active_line_breaks.reset();
    const auto value=std::string_view(source,bounded_text_length(source,length));
    try {
        active_line_breaks=formatted_boundaries(value);
    } catch(...) { log("Unicode line boundary preparation failed."); }
}
bool unicode_wrap_before(std::uint32_t last_byte) noexcept {
    if(!active_line_breaks || last_byte+1<last_scalar_bytes) return false;
    const auto offset=last_byte+1-last_scalar_bytes;
    return active_line_breaks->line_before(offset);
}
void prepare_button_wrap(const EngineString* source) noexcept {
    button_line_breaks.reset();
    try { button_line_breaks=formatted_boundaries({source->data(),static_cast<std::size_t>(source->size)}); }
    catch(...) { log("Unicode button line boundary preparation failed."); }
}
bool button_wrap_after(std::uint32_t offset) noexcept {
    return button_line_breaks&&button_line_breaks->line_before(static_cast<std::size_t>(offset)+button_extra+1);
}
std::uint64_t previous_button_slot() noexcept { return button_slot; }
std::uint64_t previous_slot() noexcept { return last_slot; }
std::uint64_t previous_extra() noexcept { return button_extra; }
char* construct_scalar(EngineString* target,const char* source) {
    const auto packed=decode_z(source);
    button_extra=static_cast<std::uint32_t>(packed>>32);
    button_slot=static_cast<std::uint32_t>(packed);
    const auto size=button_extra+1;
    auto destination=repeat_text(target,size,static_cast<unsigned char>(*source));
    std::memcpy(destination,source,size);
    return destination;
}
std::uint64_t format_scalar(const char* source) noexcept {
    const auto packed=decode_z(source);
    const auto slot=static_cast<std::uint32_t>(packed);
    // Only the engine's actual format introducers advance before its parser.
    // Ordinary Unicode characters proceed to the UTF-8 glyph iterator.
    return (slot==0xa7 || slot==0xa3 || slot==0xa4) ? packed : slot;
}
void reset_button_extra() noexcept { button_extra=0; }
bool append_icon_tail(EngineString* target,const char* source) {
    if(static_cast<unsigned char>(source[0])!=0xc2 || static_cast<unsigned char>(source[1])!=0xa3) return false;
    append_text(target,source+1,1);
    return true;
}
char* construct_map_scalar(EngineString* target,const char* source) {
    const auto packed=decode_z(source);
    const auto size=static_cast<std::uint32_t>(packed>>32)+1;
    auto destination=repeat_text(target,size,static_cast<unsigned char>(*source));
    std::memcpy(destination,source,size);
    return destination;
}
std::uint64_t copy_popup_scalar(EngineString* target,const char* source) {
    *target={};target->capacity=15;
    construct_map_scalar(target,source);
    return decode_z(source);
}
void prepare_popup_wrap(const EngineString* source) noexcept {
    popup_line_breaks.reset();
    try { popup_line_breaks=formatted_boundaries({source->data(),static_cast<std::size_t>(source->size)}); }
    catch(...) { log("Unicode popup line boundary preparation failed."); }
}
bool popup_wrap_after(std::uint32_t last_byte) noexcept { return popup_line_breaks&&popup_line_breaks->line_before(static_cast<std::size_t>(last_byte)+1); }
std::uint64_t map_scalar_size(const EngineString* source,std::size_t offset) noexcept {
    if(offset>=source->size) return 1;
    const auto scalar=eu4unicode::decode({source->data()+offset,static_cast<std::size_t>(source->size)-offset});
    return scalar.bytes?scalar.bytes:1;
}
std::uint64_t map_scalar_count(const EngineString* source) noexcept {
    auto remaining=std::string_view(source->data(),static_cast<std::size_t>(source->size));
    std::uint64_t count=0;
    while(!remaining.empty()) { const auto scalar=eu4unicode::decode(remaining); remaining.remove_prefix(scalar.bytes); ++count; }
    return count;
}
std::size_t map_last_scalar_offset(const EngineString* source) noexcept {
    const auto value=std::string_view(source->data(),static_cast<std::size_t>(source->size));
    return value.empty()?0:eu4unicode::scalar_prefix(value,value.size()-1);
}
std::size_t copy_last_map_scalar(const EngineString* source,char* destination) noexcept {
    const auto offset=map_last_scalar_offset(source);
    const auto size=static_cast<std::size_t>(source->size)-offset;
    if(size) std::memcpy(destination,source->data()+offset,size);
    destination[size]=0;
    return size;
}
void dispatch_utf8(void* window,void* receiver,const char* payload,std::uint32_t event_value) {
    std::size_t length=0;
    while(length<32 && payload[length]) ++length;
    if(length==32 || !eu4unicode::valid_utf8({payload,length})) {
        log("Rejected malformed SDL UTF-8 text input."); return;
    }
    using WindowEvent=void(*)(void*,int,std::uint32_t,int);
    using TextEvent=void(*)(void*,eu4unicode::NativeTextEvent*);
    auto window_vtable=*static_cast<void***>(window);
    auto receiver_vtable=*static_cast<void***>(receiver);
    eu4unicode::NativeTextEvent event{};
    if(!eu4unicode::make_text_event({payload,length},event)) return;
    reinterpret_cast<WindowEvent>(window_vtable[4])(window,0x303,event_value,0);
    reinterpret_cast<TextEvent>(receiver_vtable[3])(receiver,&event);
}
}

namespace {
bool initialize(HMODULE module) {
    wchar_t exe_path[32768]{},dll_path[32768]{};
    GetModuleFileNameW(nullptr,exe_path,32768);
    GetModuleFileNameW(module,dll_path,32768);
    auto exe=std::filesystem::path(exe_path);
#ifdef EU4_UNICODE_RESEARCH
    constexpr auto log_name=L"eu4_unicode_probe.log";
#else
    constexpr auto log_name=L"eu4_unicode_patch.log";
#endif
    log_file=CreateFileW((std::filesystem::path(dll_path).parent_path()/log_name).c_str(),
        GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
#ifdef EU4_UNICODE_RESEARCH
    log("EU4 UTF-8 research prototype initializing.");
    if(exe.parent_path().filename()!=L"runtime" || exe.parent_path().parent_path().filename()!=L"private" ||
       exe.parent_path().parent_path().parent_path().filename()!=L"EU4UnicodePatch") {
        log("Refused: executable is outside the isolated research fixture."); return false;
    }
#else
    log("EU4 Unicode Patch v" EU4_PATCH_VERSION " initializing; author=VulonLok.");
#endif
    if(!hash_matches(exe)) { log("Refused: executable hash mismatch."); return false; }
    if(GetModuleHandleW(L"plugin64.dll")
#ifdef EU4_UNICODE_RESEARCH
       || std::filesystem::exists(exe.parent_path()/L"plugins"/L"plugin64.dll")
#endif
    ) {
        log("Refused: legacy text patch is present."); return false;
    }
#ifndef EU4_UNICODE_RESEARCH
    player_font_directory=exe.parent_path();
    const auto fonts=std::filesystem::path(dll_path).parent_path()/L"eu4_unicode_patch"/L"fonts";
#endif
    image=reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr));
    search_dictionary_path=std::filesystem::path(dll_path).parent_path()/L"eu4_unicode_patch"/L"pinyin.txt";
#ifdef EU4_UNICODE_RESEARCH
    const bool experimental_input=GetPrivateProfileIntW(L"experimental",L"unicode_input",0,
        (std::filesystem::path(dll_path).parent_path()/L"eu4_unicode_probe.ini").c_str())!=0;
#else
    constexpr bool experimental_input=true;
#endif
    const Site sites[]={
        {0x15989d8,"b8007d0000443bf8440f4df8"},
        {0x19fad70,"40554883ec60488d6c242048"},
        {0x1595c9b,"488b85301100004883bcf82001000000"},
        {0x1595cad,"b910000000e81dd64900"},
        {0x1595ceb,"4c8bbd30110000498984ff20010000"},
        {0x16fd650,"48895c240848896c2410488974241848"},
        {0x170ca20,"48895c241848896c242048895424105657415441564157"},
        {0x170cd10,"48895c2420488954241055565741564157488d6c2480"},
        {0x170d1d0,"48895c2408574883ec2048895108488d05bb276b00488901"},
        {0x171eed0,"48896c2418565741564883ec208b69484c8bf24963f0"},
        {0x171efb0,"488b41384c8bc94885c07409448b4008442b00eb034533c0"},
        {0x153d4a0,"4863414cc3"},
        {0x15995b0,"4c63cf488b55f84c03ca4863ce410fb6014c8d1d08a7e90042880419ffc6"},
        {0x1599728,"410fb601498b8cc62001000048894d004885c9"},
        {0x159a796,"460fb60409f3410f109e680900004b8b94c620010000"},
        {0x1598963,"4c8be24c8bf1488b0d380bdb00"},
        {0x159b470,"4c8bdc49895b20555741564881ec00010000"},
        {0x159b7c0,"488bc441564881ecf00000000f2970c8"},
        {0x159b3b0,"4c8d9c2408240000410f2873e8"},
        {0x15966ec,"498bd8488bf9488b95c8210000"},
        {0x15968cb,"80bd10220000000f8481070000"},
        {0x1597d70,"80bd10220000000f840d060000"},
        {0x1598841,"4c8d9c2460220000498b5b48"},
        {0x159af87,"8b5c244883c306895c2448"},
        {0x15986f6,"8b95c8210000ffc28995c8210000"},
        {0x16d5f20,"48895c241044894c24204489442418"},
        {0x16d65d0,"4885c9745b534883ec20488bd9488b09"},
        {0x159b687,"0fb60407498b8cc6200100004885c9"},
        {0x159ef48,"f3410f10b6480800000fb604024d8b3cc64d85ff"},
        {0x159f1db,"ffc78bd7448b5310413bfa0f8d17020000"},
        {0x159f87d,"4c8b45b8f3410f10b0480800000fb604104d8b24c04d85e4"},
        {0x159fde5,"ffc38bf38b4f10440fb68d480100004533d23bd9"},
        {0x1704af0,"48895c2408574883ec40488bda443b4910"},
        {0x159eddd,"488bcb4c8b4b184983f9107203488b0b803c0aa7"},
        {0x159f6ef,"488bcf4c8b47184983f8107203488b0f8bc6803c01a7"},
        {0x159b85c,"0fb6042b3ca7750affc748ffc3e92b010000"},
        {0x159b999,"ffc748ffc349b8ffffff43ffffff0b"},
        {0x159f06d,"e85e53fffff30f58f8f3440f58c0"},
        {0x159f9a9,"e8224afffff30f58f8f3440f58c0"},
        {0x159b95a,"e8718afffff30f58f0"},
        {0x1595c86,"81ffff000000"}, {0x10b2a66,"b9883d0000"},
        {0x1b24a59,"ba883d0000"}, {0x10999f9,"ba883d0000"},
        {0x16c2cb7,"4181fe00000001"}, {0x1a683ae,"488b0d9b548d004c8bc333d2ff15b0e10f004885c0"},
        {0x1596858,"440fb60418ba01000000488d4c2448e8b49aaffe90"},
        {0x1597071,"458bce410fb604014c8b1cc14d85db"},
        {0x15983a1,"418bcff3440f109a480800000fb604014c8b04c24c894558"},
        {0x15974cb,"41ffc6443b75d80f8c59f3ffff448bbdc8210000"},
        {0x159b91a,"0fb6142b498d8f200100004c8b1cd14d85db"},
        {0x15997a9,"66837906000f85130100008d041b660f6ec8"}
        ,{0x15970df,"6641837b06000f85e0030000837db000"}
        ,{0x159c677,"488bcae86190affe8038000f84ed0600"}
        ,{0x159cd75,"4c8d9c2438040000410f2873e8410f28"}
        ,{0x159c6d8,"0fb61c07488d4d50e85b4baffe90440f"}
        ,{0x159c72b,"0fb61c07488d4d50e8084baffe90440f"}
        ,{0x159c79a,"0fb61c07488d4d50e8994aaffe90440f"}
        ,{0x159c713,"488bcee8c58faffe488bce803c07a775"}
        ,{0x159c833,"0fb604074d8ba4c7200100004d85e40f"}
        ,{0x159c8a6,"6641837c2406000f85ee000000448ba5"}
        ,{0x159c9a1,"ffc73b7e100f8c24fdffff448ba59003"}
        ,{0x159cbb3,"488d4424604983f810490f43c1803c30"}
        ,{0x159cec5,"0fb604064d8bacc7200100004d85ed75"}
        ,{0x159ce31,"c644159000498b074c8b90e0000000f3"}
        ,{0x159c899,"e8327bfffff30f58f0f30f58f8664183"}
        ,{0x159da11,"e8ba69fffff3440f58e8f3440f104424"}
        ,{0x159d9a0,"8b8d8803000083c106898d8803000083"}
        ,{0x159a240,"4e8d0409410fb6003ca77573"}
        ,{0x15996b1,"c68415d001000000498b06"}
        ,{0x159a45f,"c6840dd001000000498b06"}
        ,{0x15968e1,"488d45c84983ff10490f43c4803c03a7"}
        ,{0x1596e83,"c68415a000000000488b07"}
        ,{0x1598110,"c68415a000000000488b07"}
        ,{0x1597d7d,"488d45a04983fc10490f43c5418bcf803c08a7488d45a0"}
        ,{0x159b539,"4c8b4b18488bcb4983f9107203488b0b803c0fa7"}
        ,{0x159b61e,"498b06488d542420c6440c2000"}
        ,{0x159db79,"450fb60407ba01000000488d4c2458"}
        ,{0x159dc03,"410fb604074d8b9cc1200100004d85db"}
        ,{0x159df89,"f30f115da0410fb60401498b14c74885d2"}
        ,{0x159e436,"488d4c24784883fb10480f43ce440fb60408"}
        ,{0xfd3f40,"0fb60401888508080000f3440f10a2480800004c8b34c24d85f6"}
        ,{0xfd3eea,"488b4f1048898d680100008d41fe"}
        ,{0xfd413e,"837f10017e1b660f6ef60f5bf6488b8568010000ffc8660f6ec8"}
        ,{0xfd53c4,"ffc689b5e807000048ffc148898d18010000"}
        ,{0xfd6680,"488d85900000004983fd10480f43c60fb60418884500"}
        ,{0xfd6bc0,"488d85900000004983fd10480f43c60fb60408498b14c6"}
        ,{0xfd7330,"488d43104983f9107204488b43100fb60401498b94c420010000"}
        ,{0x159e400,"83c7060fbf420c660f6ec00f5bc0"}
        ,{0xfd53a5,"4183c506488b8d18010000488bbdc8070000"}
        ,{0x159e60d,"488bc34c8b43184983f8107203488b038bd7"}
        ,{0x159e75d,"0fb604104d8b9cc520010000f3410f108d680900004d85db"}
        ,{0x159e7c5,"e8065cfffff30f58f0f30f58f88b8d70100000"}
        ,{0x159e6f1,"4488640c40498b4500488d542440498bcd"}
        ,{0xfd6600,"8b85a0000000ffc84c63e0"}
        ,{0xfd66e4,"488d85900000004983fd10480f43c648638da00000000fb64408ff884500"}
        ,{0xfd7200,"4c89442418488954241048894c2408"}
        ,{0xfd5b70,"44894c24204489442418488954241048894c2408"}
        ,{0xfd64ca,"48c785f80000000000000048c7452000000000"}
        ,{0xfd7315,"4c634320418bfd"}
        ,{0xfd65e8,"440f2fe10f86d1020000"}
        ,{0x16d6640,"4885d20f84a60000004889742418"}
        ,{0x14ba825,"0fbe0c28488d1c28e836065900ffc788038bc7"}
        ,{0x1550425,"0fbe0c28488d1c28e80aaa4f00ffc788038bc7"}
        ,{0x1569f91,"8b45bc32db3c8073050fb6d8eb12"}
        ,{0x15366c0,"48895c240848896c24184889742420574883ec40"}
        ,{0x95110,"48895c241048896c2418565741574883ec20"}
        ,{0x153a306,"e805aeb5fe"}
        ,{0x153a717,"e8f4a9b5fe"}
        ,{0x153a99b,"e870a7b5fe"}
        ,{0xb19590,"4053565741544883ec48"}
        ,{0x1536c32,"e859295eff"}
        ,{0x15354e0,"48895c240848896c2410488974241848897c24204156"}
        ,{0x1536b80,"488bc44889580848897010488978184c896020"}
        ,{0x14e9cac,"488b01488d5310ff5008"}
        ,{0x153560a,"488d542420ff90a0000000"}
        ,{0x836f33,"0f1003488bd00f11000f104b100f1148100f1043200f1140200f104b300f1148300f1043400f114040f20f104b50f20f114850"}
        ,{0x1d91328,"22a7a4a3407b7d"}
        ,{0x14db3c0,"48895c2410488974241848897c24205541544155"}
        ,{0x1536e51,"488b07488bcfff9038010000"}
        ,{0x1536f50,"48895c2408488974241048897c24184c89642420"}
        ,{0x153aa90,"48895c240848896c2410488974241848897c2420"}
        ,{0x15384d0,"40574883ec200fb74154488bf96685c0"}
        ,{0x15385a0,"48895c2408574883ec40440fb74156"}
        ,{0x153b170,"48895c240848896c24104889742418574883ec40"}
        ,{0x15361f0,"48895c2408488974241048897c24204c89442418"}
        ,{0x1536060,"48895c240848896c2410488974241848897c2420"}
        ,{0x1535eb0,"48895c240848896c2410488974241848897c2420"}
        ,{0x1536340,"448b45d0ff50603906440f4ff3488b55"}
        ,{0x1537210,"48895c240848896c2410488974241848897c2420"}
        ,{0x1539820,"48895c241848896c242057415441574883ec204c"}
        ,{0x1539240,"40534883ec40488bd9c6819000000000e83bb21f00"}
        ,{0x1534250,"48895c242055565741564157488bec4883ec40"}
        ,{0x1534bb0,"48895c24184889742420555741544156415748"}
        ,{0x1534f90,"48895c24188954241055565741544155415641"}
        ,{0x15373a0,"40574883ec4080b90001000000488bf9"}
        ,{0x1536e70,"488b0b488b01ff5020488b4310488bd84885c075eb"}
        ,{0x159a2bf,"3c400f853e010000"}
        ,{0x159a555,"3ca40f8522010000"}
        ,{0x1dc1814,"00000041"}
        ,{0x159a3ee,"ff90f8000000"}
        ,{0x1535250,"40574883ec3080b96102000000488bf9"}
        ,{0x14db940,"48895c241044884c24205556574156"}
        ,{0x1533a0f,"e82c7ffaff"}
        ,{0x1533d90,"48895c240848896c24104889742418"}
        ,{0x15340a0,"40534883ec20c681c500000000"}
        ,{0x1534100,"48895c2408574883ec2048899198020000"}
        ,{0x14db6a0,"48895c240848897424184889542410"}
        ,{0x162f0b0,"4883ec58660f6e02488d4424080f5bc0"}
        ,{0x162f1b0,"0fb78150010000f30f1089e0000000"}
        ,{0x162f070,"3991b4010000750f488bc248c1e820"}
        ,{0x95660,"40534883ec20488b5118488bd94883"}
        ,{0x17345f0,"48ff25a1bf8700"}
        ,{0x17349f0,"48ff2541c58700"}
        ,{0x1735940,"48ff25b9ac8700"}
        ,{0x1764940,"4055564155415641574883ec20488b6c2470"}
        ,{0x1764c7c,"4d8929"}
        ,{0x17657c0,"40534883ec40488b99900300004885d2"}
        ,{0x17657f6,"0f1183f4140000"}
        ,{0x1763ab7,"c70601000000"}
        ,{0x1734490,"48ff2559bd8700"}
        ,{0x1735e00,"48ff2599ac8700"}
        ,{0x15a04f0,"40534883ec60488bda4533c0"}
        ,{0x15a05a8,"e8e38f57ff"}
        ,{0x1538560,"40534883ec20488b01488bd9ff9068010000"}
        ,{0x1538670,"40534883ec20488b01488bd9ff9068010000"}
        ,{0x153857f,"488b03488bcbff90d8000000"}
        ,{0x153868f,"488b03488bcbff90e8000000"}
        ,{0x1594360,"48895c24084889742410574883ec20488bf1488bd9bf00010000"}
        ,{0x15953c0,"48895c2408574881ec80000000488bf933db"}
        ,{0x16c3f10,"405355565741564883ec70488bf985d2"}
        ,{0x1594380,"488b0b4885c9740aba10000000e85e511a00"}
        ,{0x159487f,"488d8f20010000e8d5faffff"}
        ,{0x1174e95,"e8e6025900"}
        ,{0x13b9567,"e814bc3400"}
        ,{0x117519e,"e8ddff5800"}
        ,{0x1175c2c,"e84ff55800"}
        ,{0x1705180,"8b411085c00f840c010000"}
        ,{0x19fc097,"8bca4983c302c1e10a0bc885c97417"}
        ,{0x19fbc23,"c1e10a0bc8eb05b93f0000004c8bfa"}
        ,{0x19fbca2,"c1e10a0bc8eb05b93f0000004c8bea"}
        ,{0xefc33b,"e8b0406500"}
        ,{0xefc344,"e8278a8000"}
        ,{0xf16156,"e895a26300"}
        ,{0xf1615e,"e80dec7e00"}
        ,{0x17061a0,"48895c240848896c24104889742418"}
        ,{0xefc38f,"e80c9e800083f8ff"}
        ,{0x1141475,"e876ef4000"}
        ,{0x114147e,"e8ed385c00"}
        ,{0x11414be,"e82def4000"}
        ,{0x11414c7,"e8a4385c00"}
        ,{0x114187b,"e870eb4000"}
        ,{0x1141884,"e8e7345c00"}
        ,{0x1141b98,"e853e84000"}
        ,{0x1141ba1,"e8ca315c00"}
        ,{0x1141e9c,"e84fe54000"}
        ,{0x1141ea6,"e8c52e5c00"}
        ,{0x11434c8,"e823cf4000"}
        ,{0x1143a7c,"e86fc94000"}
        ,{0x1143a86,"e8e5125c00"}
        ,{0x1144338,"e8b3c04000"}
        ,{0x1144341,"e82a0a5c00"}
        ,{0x1141fe6,"e8b5415c0083f8ff"}
        ,{0x11420dd,"e8be405c0083f8ff"}
        ,{0x171f880,"4055565741544155415641574883ec20"}
        ,{0x114218d,"e8eed65d0042890437"}
        ,{0x11421ad,"e8ced65d00ffc0"}
        ,{0x1706010,"488bc4488958084889681048897018574883ec40"}
        ,{0xa901fe,"e80d5ec700"}
        ,{0x15a0390,"40534883ec50"}
    };
    for(const auto& site:sites) if(!check(site)) return false;
    auto address=[](std::size_t rva){ return reinterpret_cast<std::uintptr_t>(image+rva); };
    eu4unicode::native_paragraph_color=reinterpret_cast<eu4unicode::NativeParagraphColor>(address(0x15a0390));
    g_main_draw_return=address(0x159a7ac);
    g_main_copy_return=address(0x15995ce);
    g_main_measure_return=address(0x159973b);
    g_bitmap_measure_return=address(0x159b696);
    g_bitmap_split_return=address(0x159ef5c);
    g_bitmap_advance_return=address(0x159f1e6);
    g_list_measure_return=address(0x159f895);
    g_list_advance_return=address(0x159fdf4);
    g_split_format_return=address(0x159eded);
    g_split_plain_entry=address(0x159ef38);
    g_list_format_return=address(0x159f6ff);
    g_list_plain_entry=address(0x159f849);
    g_alternate_format_return=address(0x159b862);
    g_alternate_plain_entry=address(0x159b91a);
    g_alternate_end=address(0x159b9b1);
    g_alternate_advance_return=address(0x159b99e);
    g_split_kern_return=address(0x159f07b);
    g_list_kern_return=address(0x159f9b7);
    g_alternate_kern_return=address(0x159b963);
    g_button_wrap_return=address(0x15970eb);
    g_button_wrap_branch=address(0x15974cb);
    g_popup_entry_return=address(0x159c67f);
    g_popup_end_return=address(0x159cd7d);
    g_popup_data=address(0x956e0);
    g_popup_copy_return=address(0x159c6f8);
    g_popup_color_copy_return=address(0x159c74b);
    g_popup_icon_copy_return=address(0x159c7ba);
    g_popup_format_return=address(0x159c722);
    g_popup_plain_entry=address(0x159c82b);
    g_popup_measure_return=address(0x159c842);
    g_popup_wrap_return=address(0x159c8b3);
    g_popup_advance_entry=address(0x159c9a1);
    g_popup_advance_return=address(0x159c9a6);
    g_popup_draw_format_return=address(0x159cbc9);
    g_popup_draw_plain_entry=address(0x159ceaf);
    g_popup_draw_return=address(0x159ced4);
    g_popup_icon_end_return=address(0x159ce36);
    g_popup_measure_kern_return=address(0x159c8a6);
    g_popup_draw_kern_return=address(0x159da1b);
    g_popup_page_return=address(0x159d9a9);
    g_ui_vertices=address(0x235ba60);
    g_main_page_return=address(0x159af8e);
    g_button_page_return=address(0x15986fc);
    g_main_geometry_entry_return=address(0x1598969);
    g_main_geometry_end_return=address(0x159b3b8);
    g_button_geometry_entry_return=address(0x15966f2);
    g_button_geometry_end_return=address(0x1598849);
    g_copy_buffer=address(0x2433cd0);
    g_heap_pointer=address(0x233d850);
    g_heap_alloc=address(0x1b66570);
    g_heap_return=address(0x1a683c3);
    g_button_copy_return=address(0x159686c);
    g_button_measure_return=address(0x1597080);
    g_button_draw_return=address(0x15983b9);
    g_button_loop=address(0x1596831);
    g_button_end=address(0x15974df);
    g_alternate_measure_return=address(0x159b92c);
    g_wrap_return=address(0x15997bb);
    g_wrap_branch=address(0x15998c7);
    g_main_measure_entry=address(0x1599728);
    g_main_format_return=address(0x159a24a);
    g_main_plain_entry=address(0x159a791);
    g_main_icon_copy_return=address(0x15996b9);
    g_main_icon_draw_return=address(0x159a467);
    g_button_format_return=address(0x15968f1);
    g_button_plain_entry=address(0x1597059);
    g_button_icon_copy_return=address(0x1596e8b);
    g_button_icon_draw_return=address(0x1598118);
    g_button_draw_format_return=address(0x1597d94);
    g_button_draw_plain_entry=address(0x159838a);
    g_bitmap_format_return=address(0x159b54d);
    g_bitmap_plain_entry=address(0x159b677);
    g_bitmap_icon_end_return=address(0x159b62b);
    g_map_copy_return=address(0x159db8d);
    g_map_measure_return=address(0x159dc13);
    g_map_draw_return=address(0x159df9a);
    g_map_kern_return=address(0x159e45e);
    g_map_kern_call=address(0x15943d0);
    g_map_justify_draw_return=address(0xfd3f5a);
    g_map_justify_measure_return=address(0xfd4158);
    g_map_justify_count_return=address(0xfd3ef8);
    g_map_justify_single_return=address(0xfd415f);
    g_map_justify_advance_return=address(0xfd53d6);
    g_map_adjust_copy_return=address(0xfd66ab);
    g_map_adjust_glyph_return=address(0xfd6bd7);
    g_map_vertex_count_return=address(0xfd734a);
    g_map_page_tag_return=address(0x159e40e);
    g_map_justify_page_tag_return=address(0xfd53b7);
    g_map_fit_format_return=address(0x159e61f);
    g_map_fit_plain_entry=address(0x159e751);
    g_map_fit_measure_return=address(0x159e775);
    g_map_fit_kern_return=address(0x159e7d8);
    g_map_fit_icon_end_return=address(0x159e702);
    g_map_adjust_gap_end_return=address(0xfd660b);
    g_map_adjust_last_return=address(0xfd671a);
    g_map_upper_return=address(0x14ba838);
    g_map_lower_return=address(0x1550438);
    g_country_shape_return=address(0xfd64dd);
    g_province_shape_return=address(0xfd731c);
    g_country_gap_return=address(0xfd65f2);
    g_country_gap_skip=address(0xfd68c3);
    g_input_return=address(0x156a22a);
    g_editor_fit_return=address(0x1536e5d);
    g_text_limit_return=address(0x15989e4);
    g_font_allocate=address(0x1595cad);
    g_font_duplicate=address(0x1595d07);
    g_font_store_return=address(0x1595cfa);
    g_font_initialize=address(0x1595cb7);
    g_font_skip=address(0x1595f01);
    g_engine_new=address(0x1a332d4);
    g_path_pair_return=address(0x19fc0a6);
    g_wide_compare_left_return=address(0x19fbc2f);
    g_wide_compare_right_return=address(0x19fbcae);
    repeat_text=reinterpret_cast<RepeatText>(address(0x90320));
    append_text=reinterpret_cast<AppendText>(address(0x932f0));
    register_text=reinterpret_cast<RegisterText>(address(0x16fa8d0));
    if(MH_Initialize()!=MH_OK) { log("MinHook initialization failed."); return false; }
    struct Hook { std::size_t rva; void* callback; };
    const Hook hooks[]={ {0x16fd650,reinterpret_cast<void*>(import_text)},
        {0x15995b0,reinterpret_cast<void*>(main_copy_hook)}, {0x1599728,reinterpret_cast<void*>(main_measure_hook)},
        {0x159a796,reinterpret_cast<void*>(main_draw_hook)}, {0x159b687,reinterpret_cast<void*>(bitmap_measure_hook)},
        {0x159af87,reinterpret_cast<void*>(main_page_hook)},
        {0x15986f6,reinterpret_cast<void*>(button_page_hook)},
        {0x1598963,reinterpret_cast<void*>(main_geometry_entry_hook)},
        {0x159b3b0,reinterpret_cast<void*>(main_geometry_end_hook)},
        {0x15966ec,reinterpret_cast<void*>(button_geometry_entry_hook)},
        {0x1598841,reinterpret_cast<void*>(button_geometry_end_hook)},
        {0x159ef48,reinterpret_cast<void*>(bitmap_split_hook)},
        {0x159f1db,reinterpret_cast<void*>(bitmap_advance_hook)},
        {0x159f87d,reinterpret_cast<void*>(list_measure_hook)},
        {0x159fde5,reinterpret_cast<void*>(list_advance_hook)},
        {0x159eddd,reinterpret_cast<void*>(split_format_hook)},
        {0x159f6ef,reinterpret_cast<void*>(list_format_hook)},
        {0x159b85c,reinterpret_cast<void*>(alternate_format_hook)},
        {0x159b999,reinterpret_cast<void*>(alternate_advance_hook)},
        {0x159f06d,reinterpret_cast<void*>(split_kern_hook)},
        {0x159f9a9,reinterpret_cast<void*>(list_kern_hook)},
        {0x159b95a,reinterpret_cast<void*>(alternate_kern_hook)},
        {0x15970df,reinterpret_cast<void*>(button_wrap_hook)},
        {0x159c677,reinterpret_cast<void*>(popup_entry_hook)},
        {0x159cd75,reinterpret_cast<void*>(popup_end_hook)},
        {0x159c6d8,reinterpret_cast<void*>(popup_copy_hook)},
        {0x159c72b,reinterpret_cast<void*>(popup_color_copy_hook)},
        {0x159c79a,reinterpret_cast<void*>(popup_icon_copy_hook)},
        {0x159c713,reinterpret_cast<void*>(popup_format_hook)},
        {0x159c833,reinterpret_cast<void*>(popup_measure_hook)},
        {0x159c8a6,reinterpret_cast<void*>(popup_wrap_hook)},
        {0x159c9a1,reinterpret_cast<void*>(popup_advance_hook)},
        {0x159cbb3,reinterpret_cast<void*>(popup_draw_format_hook)},
        {0x159cec5,reinterpret_cast<void*>(popup_draw_hook)},
        {0x159ce31,reinterpret_cast<void*>(popup_icon_end_hook)},
        {0x159c899,reinterpret_cast<void*>(popup_measure_kern_hook)},
        {0x159da11,reinterpret_cast<void*>(popup_draw_kern_hook)},
        {0x159d9a0,reinterpret_cast<void*>(popup_page_hook)},
        {0x1a683ae,reinterpret_cast<void*>(heap_zero_hook)},
        {0x1596858,reinterpret_cast<void*>(button_copy_hook)},
        {0x1597071,reinterpret_cast<void*>(button_measure_hook)},
        {0x15983a1,reinterpret_cast<void*>(button_draw_hook)},
        {0x15974cb,reinterpret_cast<void*>(button_advance_hook)},
        {0x159b91a,reinterpret_cast<void*>(alternate_measure_hook)},
        {0x15997a9,reinterpret_cast<void*>(main_wrap_hook)},
        {0x159a240,reinterpret_cast<void*>(main_format_hook)},
        {0x15996b1,reinterpret_cast<void*>(main_icon_copy_hook)},
        {0x159a45f,reinterpret_cast<void*>(main_icon_draw_hook)},
        {0x15968e1,reinterpret_cast<void*>(button_format_hook)},
        {0x1596e83,reinterpret_cast<void*>(button_icon_copy_hook)},
        {0x1598110,reinterpret_cast<void*>(button_icon_draw_hook)},
        {0x1597d7d,reinterpret_cast<void*>(button_draw_format_hook)},
        {0x159b539,reinterpret_cast<void*>(bitmap_format_hook)},
        {0x159b61e,reinterpret_cast<void*>(bitmap_icon_end_hook)},
        {0x159db79,reinterpret_cast<void*>(map_copy_hook)},
        {0x159dc03,reinterpret_cast<void*>(map_measure_hook)},
        {0x159df89,reinterpret_cast<void*>(map_draw_hook)},
        {0x159e436,reinterpret_cast<void*>(map_kern_hook)},
        {0xfd3f40,reinterpret_cast<void*>(map_justify_draw_hook)},
        {0xfd3eea,reinterpret_cast<void*>(map_justify_count_hook)},
        {0xfd413e,reinterpret_cast<void*>(map_justify_measure_hook)},
        {0xfd53c4,reinterpret_cast<void*>(map_justify_advance_hook)},
        {0xfd6680,reinterpret_cast<void*>(map_adjust_copy_hook)},
        {0xfd6bc0,reinterpret_cast<void*>(map_adjust_glyph_hook)},
        {0xfd7330,reinterpret_cast<void*>(map_vertex_count_hook)},
        {0x159e400,reinterpret_cast<void*>(map_page_tag_hook)},
        {0xfd53a5,reinterpret_cast<void*>(map_justify_page_tag_hook)},
        {0x159e60d,reinterpret_cast<void*>(map_fit_format_hook)},
        {0x159e75d,reinterpret_cast<void*>(map_fit_measure_hook)},
        {0x159e7c5,reinterpret_cast<void*>(map_fit_kern_hook)},
        {0x159e6f1,reinterpret_cast<void*>(map_fit_icon_end_hook)},
        {0xfd6600,reinterpret_cast<void*>(map_adjust_gap_end_hook)},
        {0xfd66e4,reinterpret_cast<void*>(map_adjust_last_hook)},
        {0xfd64ca,reinterpret_cast<void*>(country_shape_hook)},
        {0xfd7315,reinterpret_cast<void*>(province_shape_hook)},
        {0xfd65e8,reinterpret_cast<void*>(country_shape_gap_hook)},
        {0x14ba825,reinterpret_cast<void*>(map_upper_hook)},
        {0x1550425,reinterpret_cast<void*>(map_lower_hook)},
        {0x15989d8,reinterpret_cast<void*>(text_limit_hook)},
        {0x1595c9b,reinterpret_cast<void*>(font_lookup_hook)},
        {0x1595cad,reinterpret_cast<void*>(font_allocate_hook)},
        {0x1595ceb,reinterpret_cast<void*>(font_store_hook)},
        {0x19fc097,reinterpret_cast<void*>(path_pair_hook)},
        {0x19fbc23,reinterpret_cast<void*>(wide_compare_left_hook)},
        {0x19fbca2,reinterpret_cast<void*>(wide_compare_right_hook)} };
    for(const auto& hook:hooks) {
        if(MH_CreateHook(image+hook.rva,hook.callback,nullptr)!=MH_OK) {
            log("Hook creation failed; no hooks enabled."); MH_Uninitialize(); return false;
        }
    }
    if(MH_CreateHook(image+0x1705180,reinterpret_cast<void*>(transliterate_save_path),
        reinterpret_cast<void**>(&original_transliterate))!=MH_OK) {
        log("Save path hook creation failed; no hooks enabled."); MH_Uninitialize(); return false;
    }
    if(MH_CreateHook(image+0x1704af0,reinterpret_cast<void*>(layout_substring),
        reinterpret_cast<void**>(&original_layout_substring))!=MH_OK) {
        log("Layout substring hook creation failed; no hooks enabled.");MH_Uninitialize();return false;
    }
    if(MH_CreateHook(image+0x159b7c0,reinterpret_cast<void*>(eu4unicode::measure_paragraph_text),
        reinterpret_cast<void**>(&eu4unicode::original_text_width))!=MH_OK||
       MH_CreateHook(image+0x159b470,reinterpret_cast<void*>(eu4unicode::measure_paragraph_height),
        reinterpret_cast<void**>(&eu4unicode::original_text_height))!=MH_OK) {
        log("Paragraph measurement hook creation failed; no hooks enabled.");MH_Uninitialize();return false;
    }
    if(MH_CreateHook(image+0x170cd10,reinterpret_cast<void*>(eu4unicode::construct_script_file),
        reinterpret_cast<void**>(&eu4unicode::original_script_file))!=MH_OK||
       MH_CreateHook(image+0x170ca20,reinterpret_cast<void*>(eu4unicode::construct_script_file_mode),
        reinterpret_cast<void**>(&eu4unicode::original_script_file_mode))!=MH_OK||
       MH_CreateHook(image+0x170d1d0,reinterpret_cast<void*>(eu4unicode::construct_script_stream),
        reinterpret_cast<void**>(&eu4unicode::original_script_stream))!=MH_OK) {
        log("Script lexer hook creation failed; no hooks enabled."); MH_Uninitialize(); return false;
    }
    if(MH_CreateHook(image+0x1706010,reinterpret_cast<void*>(convert_steam_presence),
        reinterpret_cast<void**>(&original_presence_conversion))!=MH_OK) {
        log("Steam Rich Presence hook creation failed; no hooks enabled."); MH_Uninitialize(); return false;
    }
    if(MH_CreateHook(image+0x17061a0,reinterpret_cast<void*>(find_country_name),
        reinterpret_cast<void**>(&original_find_text))!=MH_OK) {
        log("Country search hook creation failed; no hooks enabled."); MH_Uninitialize(); return false;
    }
    if(MH_CreateHook(image+0x171f880,reinterpret_cast<void*>(find_province_distance),
        reinterpret_cast<void**>(&original_search_distance))!=MH_OK) {
        log("Province search hook creation failed; no hooks enabled."); MH_Uninitialize(); return false;
    }
    if(MH_CreateHook(image+0x1594360,reinterpret_cast<void*>(destroy_font_table),
        reinterpret_cast<void**>(&original_font_table_destroy))!=MH_OK) {
        log("Font lifetime hook creation failed; no hooks enabled."); MH_Uninitialize(); return false;
    }
#ifdef EU4_UNICODE_RESEARCH
    eu4unicode::configure_font_atlases(exe.parent_path().parent_path()/L"test-mod",
        std::filesystem::path(dll_path).parent_path()/L"fonts",log);
#else
    eu4unicode::configure_font_atlases(exe.parent_path(),fonts,log,"gfx/fonts/eu4-unicode/cache/",true);
#endif
    if(MH_CreateHook(image+0x15953c0,reinterpret_cast<void*>(load_font_atlas),
        reinterpret_cast<void**>(&original_font_load))!=MH_OK||
       MH_CreateHook(image+0x16c3f10,reinterpret_cast<void*>(eu4unicode::synchronize_font_texture),
        reinterpret_cast<void**>(&eu4unicode::original_texture_lookup))!=MH_OK||
       MH_CreateHook(image+0xfd7200,reinterpret_cast<void*>(eu4unicode::build_map_font_geometry),
        reinterpret_cast<void**>(&eu4unicode::original_map_geometry))!=MH_OK||
       MH_CreateHook(image+0xfd5b70,reinterpret_cast<void*>(eu4unicode::build_country_font_geometry),
        reinterpret_cast<void**>(&eu4unicode::original_country_geometry))!=MH_OK||
       MH_CreateHook(image+0x16d6640,reinterpret_cast<void*>(eu4unicode::upload_map_font_vertices),
        reinterpret_cast<void**>(&eu4unicode::original_vertex_upload))!=MH_OK||
       MH_CreateHook(image+0x16d5f20,reinterpret_cast<void*>(eu4unicode::create_font_vertices),
        reinterpret_cast<void**>(&eu4unicode::original_vertex_create))!=MH_OK||
       MH_CreateHook(image+0x16d65d0,reinterpret_cast<void*>(eu4unicode::release_font_vertices),
        reinterpret_cast<void**>(&eu4unicode::original_vertex_release))!=MH_OK) {
        log("Dynamic font atlas hook creation failed; no hooks enabled."); MH_Uninitialize(); return false;
    }
    if(experimental_input) {
        eu4unicode::configure_native_editor_text(image);
        eu4unicode::configure_editor_presentation(image);
        if(MH_CreateHook(image+0x1764940,reinterpret_cast<void*>(eu4unicode::show_native_ime_candidates),
             reinterpret_cast<void**>(&eu4unicode::original_ime_message))!=MH_OK ||
           MH_CreateHook(image+0x17657c0,reinterpret_cast<void*>(eu4unicode::position_native_ime_candidates),
             reinterpret_cast<void**>(&eu4unicode::original_ime_rect))!=MH_OK ||
           MH_CreateHook(image+0x1569f91,reinterpret_cast<void*>(input_hook),nullptr)!=MH_OK ||
           MH_CreateHook(image+0x1536e51,reinterpret_cast<void*>(editor_fit_hook),nullptr)!=MH_OK ||
           MH_CreateHook(image+0x15366c0,reinterpret_cast<void*>(editor_key),
             reinterpret_cast<void**>(&original_editor_key))!=MH_OK ||
           MH_CreateHook(image+0x15384d0,reinterpret_cast<void*>(editor_left),
             reinterpret_cast<void**>(&original_editor_left))!=MH_OK ||
           MH_CreateHook(image+0x15385a0,reinterpret_cast<void*>(editor_right),
             reinterpret_cast<void**>(&original_editor_right))!=MH_OK ||
           MH_CreateHook(image+0x153b170,reinterpret_cast<void*>(editor_selection),
             reinterpret_cast<void**>(&original_editor_selection))!=MH_OK ||
           MH_CreateHook(image+0x15361f0,reinterpret_cast<void*>(editor_point),
             reinterpret_cast<void**>(&original_editor_point))!=MH_OK ||
           MH_CreateHook(image+0x1536060,reinterpret_cast<void*>(editor_caret_position),
             reinterpret_cast<void**>(&original_editor_caret))!=MH_OK ||
           MH_CreateHook(image+0x1535eb0,reinterpret_cast<void*>(editor_anchor_position),
             reinterpret_cast<void**>(&original_editor_anchor))!=MH_OK ||
           MH_CreateHook(image+0x1537210,reinterpret_cast<void*>(editor_width_fit),
             reinterpret_cast<void**>(&original_editor_width_fit))!=MH_OK ||
           MH_CreateHook(image+0x1539820,reinterpret_cast<void*>(editor_word_break),
             reinterpret_cast<void**>(&original_editor_word_break))!=MH_OK ||
           MH_CreateHook(image+0x1539240,reinterpret_cast<void*>(paste_editor_clipboard),nullptr)!=MH_OK ||
           MH_CreateHook(image+0x1534250,reinterpret_cast<void*>(paint_editor_ime_rect),
             reinterpret_cast<void**>(&original_editor_paint))!=MH_OK ||
           MH_CreateHook(image+0x1534bb0,reinterpret_cast<void*>(draw_editor_text),
             reinterpret_cast<void**>(&original_editor_text))!=MH_OK ||
           MH_CreateHook(image+0x1534f90,reinterpret_cast<void*>(draw_editor_lines),
             reinterpret_cast<void**>(&original_editor_lines))!=MH_OK ||
           MH_CreateHook(image+0x1535250,reinterpret_cast<void*>(focus_editor_ime_rect),
             reinterpret_cast<void**>(&original_editor_focus))!=MH_OK ||
           MH_CreateHook(image+0x14db940,reinterpret_cast<void*>(create_editor_sprite),
             reinterpret_cast<void**>(&original_editor_sprite_factory))!=MH_OK ||
           MH_CreateHook(image+0x1533d90,reinterpret_cast<void*>(destroy_editor),
             reinterpret_cast<void**>(&original_editor_destroy))!=MH_OK ||
           MH_CreateHook(image+0x15340a0,reinterpret_cast<void*>(hide_editor),
             reinterpret_cast<void**>(&original_editor_hide))!=MH_OK ||
           MH_CreateHook(image+0x1534100,reinterpret_cast<void*>(setup_editor),
             reinterpret_cast<void**>(&original_editor_setup))!=MH_OK ||
           MH_CreateHook(image+0x95110,reinterpret_cast<void*>(assign_editor_prefix),
             reinterpret_cast<void**>(&original_assign_text))!=MH_OK ||
           MH_CreateHook(image+0xb19590,reinterpret_cast<void*>(filter_editor_text),
             reinterpret_cast<void**>(&original_filter_text))!=MH_OK ||
           MH_CreateHook(image+0x15354e0,reinterpret_cast<void*>(consume_editor_commit),
             reinterpret_cast<void**>(&original_editor_character))!=MH_OK ||
           MH_CreateHook(image+0x1536b80,reinterpret_cast<void*>(insert_editor_commit),
             reinterpret_cast<void**>(&original_editor_insert))!=MH_OK) {
            log("Input hook creation failed; no hooks enabled."); MH_Uninitialize(); return false;
        }
        editor_selections=std::make_unique<eu4unicode::NativeEditorSelections>(original_editor_sprite_factory,
            reinterpret_cast<eu4unicode::NativeSpriteDestroy>(image+0x14db6a0),
            reinterpret_cast<eu4unicode::NativeStringDestroy>(image+0x95660));
        log("UTF-8 input, multiline grapheme editing, IME presentation and undo enabled.");
        log("Native Windows IME candidate UI and caret exclusion rectangle enabled.");
    }
    struct DataPatch { std::size_t rva; std::vector<std::byte> before,after; };
    // Allocate all patch/rollback buffers before modifying any instruction.
    const DataPatch constants[]={ {0x1595c88,bytes("ff000000"),bytes("ffff1000")},
        {0x16c2cba,bytes("00000001"),bytes("00000004")},
        // Save-name builders and save/load selection call the CP1252 transliterator.
        // Skip only those calls: their strings already contain UTF-8. The
        // later filename-character validation and other callers stay native.
        {0x1174e95,bytes("e8e6025900"),bytes("9090909090")},
        {0x13b9567,bytes("e814bc3400"),bytes("9090909090")},
        {0x117519e,bytes("e8ddff5800"),bytes("9090909090")},
        {0x1175c2c,bytes("e84ff55800"),bytes("9090909090")},
        // Keep both the original query and localized candidate in UTF-8;
        // derive display-search keys only in the scoped matching callback.
        {0xefc33b,bytes("e8b0406500"),bytes("9090909090")},
        {0xefc344,bytes("e8278a8000"),bytes("9090909090")},
        {0xf16156,bytes("e895a26300"),bytes("9090909090")},
        {0xf1615e,bytes("e80dec7e00"),bytes("9090909090")},
        // Province-finder names, alternative names and queries retain UTF-8.
        {0x1141475,bytes("e876ef4000"),bytes("9090909090")},
        {0x114147e,bytes("e8ed385c00"),bytes("9090909090")},
        {0x11414be,bytes("e82def4000"),bytes("9090909090")},
        {0x11414c7,bytes("e8a4385c00"),bytes("9090909090")},
        {0x114187b,bytes("e870eb4000"),bytes("9090909090")},
        {0x1141884,bytes("e8e7345c00"),bytes("9090909090")},
        {0x1141b98,bytes("e853e84000"),bytes("9090909090")},
        {0x1141ba1,bytes("e8ca315c00"),bytes("9090909090")},
        {0x1141e9c,bytes("e84fe54000"),bytes("9090909090")},
        {0x1141ea6,bytes("e8c52e5c00"),bytes("9090909090")},
        {0x11434c8,bytes("e823cf4000"),bytes("9090909090")},
        {0x1143a7c,bytes("e86fc94000"),bytes("9090909090")},
        {0x1143a86,bytes("e8e5125c00"),bytes("9090909090")},
        {0x1144338,bytes("e8b3c04000"),bytes("9090909090")},
        {0x1144341,bytes("e82a0a5c00"),bytes("9090909090")} };
    std::size_t applied=0;
    bool constants_ok=true;
    for(const auto& patch:constants) {
        ++applied;
        if(!write(patch.rva,patch.after.data(),patch.after.size())) { constants_ok=false; break; }
    }
    const bool enabled=constants_ok && MH_EnableHook(MH_ALL_HOOKS)==MH_OK;
    if(!enabled) {
        log("Patch activation failed; restoring original instructions and constants.");
        MH_DisableHook(MH_ALL_HOOKS);
        while(applied) {
            const auto& patch=constants[--applied];
            write(patch.rva,patch.before.data(),patch.before.size());
        }
        MH_Uninitialize();
        return false;
    }
    patch_enabled.store(true,std::memory_order_release);
    log("UTF-8 import, UI, format, map and bitmap iterators enabled.");
    log("Script lexer UTF-8 BOM handling enabled.");
    log("Steam Rich Presence UTF-8 passthrough enabled.");
    log("Chinese and pinyin country/province search enabled.");
    return true;
}
}
extern "C" __declspec(dllexport) int Eu4UnicodeProbeEnabled() noexcept {
    return patch_enabled.load(std::memory_order_acquire)?1:0;
}
extern "C" __declspec(dllexport) std::uint64_t Eu4UnicodeProbeGlyphFonts() noexcept {
    return eu4unicode::unicode_glyph_usage().fonts;
}
extern "C" __declspec(dllexport) std::uint64_t Eu4UnicodeProbeGlyphRecords() noexcept {
    return eu4unicode::unicode_glyph_usage().glyphs;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) {
        // thread_local is intentionally retained; do not disable thread notifications.
        try { initialize(module); } catch(...) { log("Initialization exception; patch disabled."); }
    }
    return TRUE;
}
