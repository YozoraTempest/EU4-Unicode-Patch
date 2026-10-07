#include "native_ime.hpp"
#include <imm.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>

namespace eu4unicode {
NativeImeMessage original_ime_message=nullptr;
NativeImeRect original_ime_rect=nullptr;
namespace {
std::mutex composition_mutex;
NativeComposition composition;
void update_composition(HWND window,UINT message,WPARAM parameter,LPARAM flags,bool active) noexcept {
    try {
        if(message==WM_IME_ENDCOMPOSITION||message==WM_KILLFOCUS||
           message==WM_INPUTLANGCHANGE||(message==WM_IME_SETCONTEXT&&!parameter)||!active) {
            clear_native_composition();return;
        }
        if(message==WM_IME_STARTCOMPOSITION) {
            std::lock_guard<std::mutex> lock(composition_mutex);composition={window,{},true};return;
        }
        if(message!=WM_IME_COMPOSITION) return;
        if(!flags||(flags&GCS_RESULTSTR)) { clear_native_composition();if(!(flags&GCS_COMPSTR)) return; }
        const auto context=ImmGetContext(window);
        if(!context) { clear_native_composition();return; }
        struct Release { HWND window;HIMC context;~Release(){ImmReleaseContext(window,context);} } release{window,context};
        const auto bytes=ImmGetCompositionStringW(context,GCS_COMPSTR,nullptr,0);
        if(bytes<0||bytes>64000||bytes%sizeof(char16_t)) { clear_native_composition();return; }
        std::u16string text(static_cast<std::size_t>(bytes)/sizeof(char16_t),u'\0');
        if(bytes&&ImmGetCompositionStringW(context,GCS_COMPSTR,text.data(),bytes)!=bytes) { clear_native_composition();return; }
        const auto cursor=ImmGetCompositionStringW(context,GCS_CURSORPOS,nullptr,0);
        if(cursor<0||static_cast<std::size_t>(cursor)>text.size()) { clear_native_composition();return; }
        auto value=composition_text(text,static_cast<std::size_t>(cursor));
        std::lock_guard<std::mutex> lock(composition_mutex);composition={window,std::move(value),true};
    } catch(...) { clear_native_composition(); }
}
bool ime_enabled(void* video) {
    if(!video) return false;
    const auto data=static_cast<const std::byte*>(video);
    // SDL 2.0.4 in the guarded EU4 1.37.5 executable: initialized,
    // enabled, available. These offsets are not a generic SDL ABI.
    return *reinterpret_cast<const int*>(data+0x48)!=0&&
           *reinterpret_cast<const int*>(data+0x4c)!=0&&
           *reinterpret_cast<const int*>(data+0x50)!=0;
}
void position_ime(HWND window,const ImeRect& rect,std::uint32_t candidate_mask=1) {
    if(!window||rect.w<=0||rect.h<=0) return;
    const auto right=static_cast<std::int64_t>(rect.x)+rect.w;
    const auto bottom=static_cast<std::int64_t>(rect.y)+rect.h;
    if(right>std::numeric_limits<LONG>::max()||bottom>std::numeric_limits<LONG>::max()) return;
    // IMM setters send window messages synchronously. Avoid recursively updating
    // geometry if an IME reports a candidate change during a position request.
    static thread_local bool positioning=false;
    if(positioning) return;
    positioning=true;
    struct Reset { bool& flag;~Reset(){flag=false;} } reset{positioning};
    const auto context=ImmGetContext(window);
    if(!context) return;
    struct Release { HWND window;HIMC context;~Release(){ImmReleaseContext(window,context);} } release{window,context};
    COMPOSITIONFORM composition_form{};
    composition_form.dwStyle=CFS_FORCE_POSITION;
    composition_form.ptCurrentPos={rect.x,rect.y};
    ImmSetCompositionWindow(context,&composition_form);
    // IMN_OPEN/CHANGECANDIDATE identifies candidate lists by a bit mask.
    // Do not assume that every third-party IME uses list zero.
    if(!candidate_mask) candidate_mask=1;
    for(DWORD index=0;index<4;++index) {
        if(!(candidate_mask&(1u<<index))) continue;
        CANDIDATEFORM candidate{};
        candidate.dwIndex=index;
        candidate.dwStyle=CFS_EXCLUDE;
        candidate.ptCurrentPos={rect.x,rect.y};
        candidate.rcArea={rect.x,rect.y,static_cast<LONG>(right),static_cast<LONG>(bottom)};
        ImmSetCandidateWindow(context,&candidate);
    }
}
void restore_ime_position(HWND window,void* video,std::uint32_t candidate_mask=1) {
    ImeRect rect{};
    // WIN_SetTextInputRect stores the latest client rectangle here even before
    // text input is enabled. A newly associated HIMC needs it applied again.
    std::memcpy(&rect,static_cast<const std::byte*>(video)+0x14f4,sizeof(rect));
    position_ime(window,rect,candidate_mask);
}
}
int show_native_ime_candidates(HWND window,UINT message,WPARAM parameter,LPARAM* flags,void* video) {
    const bool active=ime_enabled(video);
    const auto incoming=flags?*flags:0;
    update_composition(window,message,parameter,incoming,active);
    const auto trapped=original_ime_message(window,message,parameter,flags,video);
    // SDL clears all WM_IME_SETCONTEXT UI flags. Preserve the caller's
    // requested native candidate/reading UI; SDL still owns text commits.
    if(active&&flags&&message==WM_IME_SETCONTEXT)
        *flags=incoming&~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW);
    if(active) {
        const bool candidates=message==WM_IME_NOTIFY&&
            (parameter==IMN_OPENCANDIDATE||parameter==IMN_CHANGECANDIDATE);
        if(message==WM_IME_STARTCOMPOSITION||message==WM_INPUTLANGCHANGE||
           (message==WM_IME_SETCONTEXT&&parameter)||candidates)
            restore_ime_position(window,video,candidates?static_cast<std::uint32_t>(incoming):1);
        // Third-party IMEs also need start/private/status notifications in their
        // default window. SDL still receives each event first and owns commits;
        // WM_IME_COMPOSITION remains trapped to prevent duplicate result text.
        if(message==WM_IME_STARTCOMPOSITION||message==WM_IME_NOTIFY) return 0;
    }
    return trapped;
}
NativeComposition native_composition() { std::lock_guard<std::mutex> lock(composition_mutex);return composition; }
void clear_native_composition() noexcept { try { std::lock_guard<std::mutex> lock(composition_mutex);composition={}; } catch(...) {} }
void position_native_ime_candidates(void* device,const ImeRect* rect) {
    original_ime_rect(device,rect);
    if(!device||!rect) return;
    const auto video=*reinterpret_cast<std::byte**>(static_cast<std::byte*>(device)+0x390);
    if(!ime_enabled(video)) return;
    const auto window=*reinterpret_cast<HWND*>(video+0x60);
    position_ime(window,*rect);
}
}
