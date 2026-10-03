#include "native_ime.hpp"
#include <imm.h>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace eu4unicode {
NativeImeMessage original_ime_message=nullptr;
NativeImeRect original_ime_rect=nullptr;
namespace {
bool ime_enabled(void* video) {
    if(!video) return false;
    const auto data=static_cast<const std::byte*>(video);
    // SDL 2.0.4 in the guarded EU4 1.37.5 executable: initialized,
    // enabled, available. These offsets are not a generic SDL ABI.
    return *reinterpret_cast<const int*>(data+0x48)!=0&&
           *reinterpret_cast<const int*>(data+0x4c)!=0&&
           *reinterpret_cast<const int*>(data+0x50)!=0;
}
}
int show_native_ime_candidates(HWND window,UINT message,WPARAM parameter,LPARAM* flags,void* video) {
    const bool active=ime_enabled(video);
    const auto incoming=flags?*flags:0;
    const auto trapped=original_ime_message(window,message,parameter,flags,video);
    // SDL clears all WM_IME_SETCONTEXT UI flags. Preserve the caller's
    // requested native candidate/reading UI; SDL still owns text commits.
    if(active&&flags&&message==WM_IME_SETCONTEXT) *flags=incoming;
    // The default IME window must receive candidate lifecycle messages.
    // Keep SDL's bookkeeping and other messages, including result strings.
    if(active&&message==WM_IME_NOTIFY&&
       (parameter==IMN_OPENCANDIDATE||parameter==IMN_CHANGECANDIDATE||parameter==IMN_CLOSECANDIDATE)) return 0;
    return trapped;
}
void position_native_ime_candidates(void* device,const ImeRect* rect) {
    original_ime_rect(device,rect);
    if(!device||!rect||rect->w<=0||rect->h<=0) return;
    const auto right=static_cast<std::int64_t>(rect->x)+rect->w;
    const auto bottom=static_cast<std::int64_t>(rect->y)+rect->h;
    if(right>std::numeric_limits<LONG>::max()||bottom>std::numeric_limits<LONG>::max()) return;
    const auto video=*reinterpret_cast<std::byte**>(static_cast<std::byte*>(device)+0x390);
    if(!ime_enabled(video)) return;
    const auto window=*reinterpret_cast<HWND*>(video+0x60);
    if(!window) return;
    const auto context=ImmGetContext(window);
    if(!context) return;
    CANDIDATEFORM candidate{};
    candidate.dwIndex=0;
    candidate.dwStyle=CFS_EXCLUDE;
    candidate.ptCurrentPos={rect->x,rect->y};
    candidate.rcArea={rect->x,rect->y,static_cast<LONG>(right),static_cast<LONG>(bottom)};
    ImmSetCandidateWindow(context,&candidate);
    ImmReleaseContext(window,context);
}
}
