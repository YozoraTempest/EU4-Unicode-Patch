#include "native_ime.hpp"
#include <imm.h>
#include <MinHook.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
HIMC modeled_context=nullptr;
std::u16string preedit;
LONG cursor=0;
int blur_calls=0;
std::optional<LONG> length_override;
decltype(&ImmGetCompositionStringW) original_read=nullptr;
void blur_editor(void* owner) { ++blur_calls;eu4unicode::blur_native_editor(owner); }
LONG WINAPI read_preedit(HIMC context,DWORD index,LPVOID buffer,DWORD capacity) {
    if(context!=modeled_context) return original_read(context,index,buffer,capacity);
    if(index==GCS_CURSORPOS) return cursor;
    if(index!=GCS_COMPSTR) return IMM_ERROR_NODATA;
    if(!buffer) return length_override?*length_override:static_cast<LONG>(preedit.size()*sizeof(char16_t));
    const auto bytes=static_cast<DWORD>(preedit.size()*sizeof(char16_t));
    if(capacity<bytes) return IMM_ERROR_GENERAL;
    std::memcpy(buffer,preedit.data(),bytes);return static_cast<LONG>(bytes);
}
struct Fixture {
    HWND window=CreateWindowExW(0,L"STATIC",L"Preedit contract",0,0,0,100,100,nullptr,nullptr,nullptr,nullptr);
    HIMC first=ImmCreateContext(),second=ImmCreateContext(),previous=nullptr;
    void* target=nullptr;
    alignas(void*) std::array<std::byte,0x1510> video{};
    Fixture() {
        require(window&&first&&second,"Cannot create composition fixture");
        previous=ImmAssociateContext(window,first);modeled_context=first;
        for(const auto offset:{0x48,0x4c,0x50}) *reinterpret_cast<int*>(video.data()+offset)=1;
        // Supply deterministic IMM responses in this test process. Real IME
        // installations and active keyboard layouts vary between CI hosts.
        require(MH_Initialize()==MH_OK,"Cannot initialize composition reader hook");
        target=reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"imm32.dll"),"ImmGetCompositionStringW"));
        require(target&&MH_CreateHook(target,reinterpret_cast<void*>(read_preedit),
            reinterpret_cast<void**>(&original_read))==MH_OK,"Cannot hook composition reader");
        require(MH_EnableHook(target)==MH_OK,"Cannot enable composition reader hook");
    }
    ~Fixture() {
        MH_DisableHook(target);MH_RemoveHook(target);MH_Uninitialize();modeled_context=nullptr;
        eu4unicode::clear_native_composition();
        ImmAssociateContext(window,previous);ImmDestroyContext(first);ImmDestroyContext(second);DestroyWindow(window);
    }
    int send(UINT message,WPARAM parameter=0,LPARAM flags=0) {
        return eu4unicode::show_native_ime_candidates(window,message,parameter,&flags,video.data());
    }
    void compose() {
        preedit=u"zhongwen";cursor=3;length_override.reset();
        send(WM_IME_STARTCOMPOSITION);
        require(!eu4unicode::native_composition().active,"Composition start alone must not block editing keys");
        send(WM_IME_COMPOSITION,0,GCS_COMPSTR|GCS_CURSORPOS);
        const auto state=eu4unicode::native_composition();
        require(state.active&&state.value.text=="zhongwen"&&state.value.caret==3&&
            eu4unicode::native_ime_owns_edit_keys(),"Live preedit must retain its editing keys and cursor");
    }
    void released() {
        require(!eu4unicode::native_ime_owns_edit_keys()&&!eu4unicode::native_composition().active,
            "Finished or invalid preedit must release editing keys");
    }
};
}
void verify_native_ime_composition() {
    Fixture fixture;
    fixture.compose();preedit.clear();cursor=0;
    fixture.send(WM_IME_COMPOSITION,0,GCS_COMPSTR);fixture.released();
    // Same-language TSF switches need not change HKL or send an end event.
    fixture.compose();preedit.clear();fixture.released();
    fixture.compose();ImmAssociateContext(fixture.window,fixture.second);fixture.released();
    ImmAssociateContext(fixture.window,fixture.first);
    fixture.compose();ImmAssociateContext(fixture.window,nullptr);fixture.released();
    ImmAssociateContext(fixture.window,fixture.first);
    for(const auto message:{WM_IME_ENDCOMPOSITION,WM_KILLFOCUS,WM_INPUTLANGCHANGE,
                           WM_IME_STARTCOMPOSITION}) {
        fixture.compose();fixture.send(message);fixture.released();
    }
    for(const WPARAM activation:{WPARAM{0},WPARAM{1}}) {
        fixture.compose();fixture.send(WM_IME_SETCONTEXT,activation,0xc000000f);fixture.released();
    }
    fixture.compose();fixture.send(WM_IME_COMPOSITION);fixture.released();
    fixture.compose();fixture.send(WM_IME_COMPOSITION,0,GCS_RESULTSTR);fixture.released();
    fixture.compose();fixture.send(WM_IME_COMPOSITION,0,GCS_RESULTSTR|GCS_COMPSTR);
    require(eu4unicode::native_ime_owns_edit_keys(),"A commit with new preedit must keep the new composition active");
    for(const LONG invalid:{LONG{0},LONG{IMM_ERROR_NODATA},LONG{IMM_ERROR_GENERAL},LONG{1},LONG{64002}}) {
        fixture.compose();length_override=invalid;fixture.released();
        fixture.send(WM_IME_COMPOSITION,0,GCS_COMPSTR);fixture.released();
    }
    fixture.compose();cursor=99;fixture.send(WM_IME_COMPOSITION,0,GCS_COMPSTR|GCS_CURSORPOS);fixture.released();
    int editor=0;
    eu4unicode::focus_native_editor(&editor);fixture.compose();eu4unicode::blur_native_editor(&editor);fixture.released();
    const auto saved_blur=eu4unicode::native_editor_blur;
    eu4unicode::native_editor_blur=blur_editor;
    require(!eu4unicode::exit_native_editor(),"Escape without a focused editor must remain a game key");
    require(fixture.send(WM_KEYDOWN,VK_ESCAPE,0x10001)==0,"Unfocused Escape must reach the native game key handler");
    eu4unicode::focus_native_editor(&editor);
    fixture.send(WM_KEYDOWN,'A',0x1e0001);
    require(eu4unicode::focused_native_editor()==&editor&&blur_calls==0,"Typing must keep editor focus");
    require(fixture.send(WM_KEYDOWN,VK_ESCAPE,0x10001)==1,"Focused Escape must be consumed before game shortcuts");
    require(!eu4unicode::focused_native_editor()&&blur_calls==1,"Escape must release the native editor focus");
    fixture.released();
    eu4unicode::focus_native_editor(&editor);fixture.compose();
    fixture.send(WM_KEYDOWN,VK_PROCESSKEY,0x1e0001);
    require(eu4unicode::focused_native_editor()==&editor&&blur_calls==1,"An IME-owned letter must keep focus");
    require(fixture.send(WM_KEYDOWN,VK_PROCESSKEY,0x10001)==1,"IME-owned Escape must be consumed before game shortcuts");
    require(!eu4unicode::focused_native_editor()&&blur_calls==2,"IME-owned Escape must cancel preedit and release focus");
    fixture.released();eu4unicode::native_editor_blur=saved_blur;
}
