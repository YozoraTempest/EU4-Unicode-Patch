#include "native_ime.hpp"
#include <imm.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
void verify_native_editor_selections();

namespace {
int calls=0,commits=0;
HIMC next_context=nullptr;
int native_handler(HWND window,UINT message,WPARAM,LPARAM* flags,void* video) {
    ++calls;
    if(!video) return 0;
    const auto data=static_cast<const std::byte*>(video);
    if(*reinterpret_cast<const int*>(data+0x48)==0||
       *reinterpret_cast<const int*>(data+0x4c)==0||
       *reinterpret_cast<const int*>(data+0x50)==0) return 0;
    if(message==WM_IME_SETCONTEXT) { *flags=0;return 0; }
    if(message==WM_INPUTLANGCHANGE&&next_context) ImmAssociateContext(window,next_context);
    if(message==WM_IME_COMPOSITION&&(*flags&GCS_RESULTSTR)) ++commits;
    return 1;
}
void require(bool condition) { if(!condition) throw std::runtime_error("IME message contract failed"); }
void native_rect(void* device,const eu4unicode::ImeRect* rect) {
    const auto video=*reinterpret_cast<std::byte**>(static_cast<std::byte*>(device)+0x390);
    std::memcpy(video+0x14f4,rect,sizeof(*rect));
}
struct ImeWindow {
    HWND window=CreateWindowExW(0,L"STATIC",L"IME contract",0,0,0,100,100,nullptr,nullptr,nullptr,nullptr);
    HIMC first=ImmCreateContext(),second=ImmCreateContext(),previous=nullptr;
    ImeWindow() {
        require(window&&first&&second);previous=ImmAssociateContext(window,first);
    }
    ~ImeWindow() {
        ImmAssociateContext(window,previous);ImmDestroyContext(first);ImmDestroyContext(second);DestroyWindow(window);
    }
};
void geometry(HIMC context,DWORD index,const eu4unicode::ImeRect& rect) {
    CANDIDATEFORM candidate{};COMPOSITIONFORM composition{};
    require(ImmGetCandidateWindow(context,index,&candidate)!=FALSE);
    require(candidate.dwIndex==index&&candidate.dwStyle==CFS_EXCLUDE&&
        candidate.ptCurrentPos.x==rect.x&&candidate.ptCurrentPos.y==rect.y&&
        candidate.rcArea.left==rect.x&&candidate.rcArea.top==rect.y&&
        candidate.rcArea.right==rect.x+rect.w&&candidate.rcArea.bottom==rect.y+rect.h);
    require(ImmGetCompositionWindow(context,&composition)!=FALSE&&
        composition.dwStyle==CFS_FORCE_POSITION&&composition.ptCurrentPos.x==rect.x&&composition.ptCurrentPos.y==rect.y);
}
}
int main() {
    try {
        verify_native_editor_selections();
        alignas(void*) std::array<std::byte,0x1510> video{};
        for(const auto offset:{0x48,0x4c,0x50}) *reinterpret_cast<int*>(video.data()+offset)=1;
        eu4unicode::original_ime_message=native_handler;
        LPARAM lifecycle=0;
        require(eu4unicode::show_native_ime_candidates(nullptr,WM_IME_STARTCOMPOSITION,0,&lifecycle,video.data())==0);
        require(eu4unicode::native_composition().active);
        require(eu4unicode::show_native_ime_candidates(nullptr,WM_IME_ENDCOMPOSITION,0,&lifecycle,video.data())==1);
        require(!eu4unicode::native_composition().active);
        eu4unicode::show_native_ime_candidates(nullptr,WM_IME_STARTCOMPOSITION,0,&lifecycle,video.data());
        eu4unicode::show_native_ime_candidates(nullptr,WM_KILLFOCUS,0,&lifecycle,video.data());
        require(!eu4unicode::native_composition().active);
        for(const LPARAM incoming:{LPARAM{0},LPARAM{1},LPARAM{0xf},LPARAM{0xc000000f}}) {
            auto flags=incoming;const auto before=calls;
            require(eu4unicode::show_native_ime_candidates(nullptr,WM_IME_SETCONTEXT,1,&flags,video.data())==0);
            require(flags==(incoming&~static_cast<LPARAM>(ISC_SHOWUICOMPOSITIONWINDOW))&&calls==before+1);
        }
        for(const auto event:{IMN_OPENCANDIDATE,IMN_CHANGECANDIDATE,IMN_CLOSECANDIDATE}) {
            LPARAM flags=3;const auto before=calls;
            require(eu4unicode::show_native_ime_candidates(nullptr,WM_IME_NOTIFY,event,&flags,video.data())==0);
            require(flags==3&&calls==before+1);
        }
        for(const auto event:{IMN_SETOPENSTATUS,IMN_SETCONVERSIONMODE,IMN_PRIVATE,
                              IMN_OPENSTATUSWINDOW,IMN_CLOSESTATUSWINDOW,IMN_SETCANDIDATEPOS}) {
            LPARAM flags=17;
            require(eu4unicode::show_native_ime_candidates(nullptr,WM_IME_NOTIFY,event,&flags,video.data())==0&&flags==17);
        }
        LPARAM result=GCS_RESULTSTR|GCS_COMPSTR;
        require(eu4unicode::show_native_ime_candidates(nullptr,WM_IME_COMPOSITION,0,&result,video.data())==1);
        require(result==(GCS_RESULTSTR|GCS_COMPSTR)&&commits==1);
        for(const auto offset:{0x48,0x4c,0x50}) {
            *reinterpret_cast<int*>(video.data()+offset)=0;
            LPARAM flags=0xc000000f;
            require(eu4unicode::show_native_ime_candidates(nullptr,WM_IME_SETCONTEXT,0,&flags,video.data())==0);
            require(flags==0xc000000f);
            *reinterpret_cast<int*>(video.data()+offset)=1;
        }
        LPARAM flags=0xf;
        require(eu4unicode::show_native_ime_candidates(nullptr,WM_IME_SETCONTEXT,0,&flags,nullptr)==0&&flags==0xf);
        ImeWindow ime;
        *reinterpret_cast<HWND*>(video.data()+0x60)=ime.window;
        alignas(void*) std::array<std::byte,0x3a0> device{};
        *reinterpret_cast<std::byte**>(device.data()+0x390)=video.data();
        eu4unicode::original_ime_rect=native_rect;
        const eu4unicode::ImeRect first{10,20,2,18},second{70,80,3,22};
        eu4unicode::position_native_ime_candidates(device.data(),&first);
        geometry(ime.first,0,first);
        // Model SDL applying a new HIMC while processing an input-language
        // change. Candidate geometry must be restored after that processing.
        next_context=ime.second;
        std::memcpy(video.data()+0x14f4,&second,sizeof(second));
        require(eu4unicode::show_native_ime_candidates(ime.window,WM_INPUTLANGCHANGE,0,&lifecycle,video.data())==1);
        next_context=nullptr;geometry(ime.second,0,second);geometry(ime.first,0,first);
        const eu4unicode::ImeRect start{100,120,1,24};
        std::memcpy(video.data()+0x14f4,&start,sizeof(start));
        require(eu4unicode::show_native_ime_candidates(ime.window,WM_IME_STARTCOMPOSITION,0,&lifecycle,video.data())==0);
        geometry(ime.second,0,start);
        LPARAM lists=0b1010;
        require(eu4unicode::show_native_ime_candidates(ime.window,WM_IME_NOTIFY,IMN_OPENCANDIDATE,&lists,video.data())==0);
        geometry(ime.second,1,start);geometry(ime.second,3,start);
        const eu4unicode::ImeRect changed{150,170,2,20};
        std::memcpy(video.data()+0x14f4,&changed,sizeof(changed));lists=0b0110;
        eu4unicode::show_native_ime_candidates(ime.window,WM_IME_NOTIFY,IMN_CHANGECANDIDATE,&lists,video.data());
        geometry(ime.second,1,changed);geometry(ime.second,2,changed);geometry(ime.second,3,start);
        const eu4unicode::ImeRect invalid{(std::numeric_limits<int>::max)(),170,2,20};
        eu4unicode::position_native_ime_candidates(device.data(),&invalid);geometry(ime.second,0,start);
        std::puts("PASS: native IME UI flags, lifecycle propagation, context replacement, candidate-list geometry and single commit dispatch.");
    } catch(const std::exception& error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
}
