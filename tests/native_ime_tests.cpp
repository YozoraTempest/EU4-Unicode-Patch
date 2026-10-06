#include "native_ime.hpp"
#include <imm.h>
#include <array>
#include <cstddef>
#include <cstdio>
#include <stdexcept>
void verify_native_editor_selections();

namespace {
int calls=0,commits=0;
int native_handler(HWND,UINT message,WPARAM,LPARAM* flags,void* video) {
    ++calls;
    if(!video) return 0;
    const auto data=static_cast<const std::byte*>(video);
    if(*reinterpret_cast<const int*>(data+0x48)==0||
       *reinterpret_cast<const int*>(data+0x4c)==0||
       *reinterpret_cast<const int*>(data+0x50)==0) return 0;
    if(message==WM_IME_SETCONTEXT) { *flags=0;return 0; }
    if(message==WM_IME_COMPOSITION&&(*flags&GCS_RESULTSTR)) ++commits;
    return 1;
}
void require(bool condition) { if(!condition) throw std::runtime_error("IME message contract failed"); }
}
int main() {
    try {
        verify_native_editor_selections();
        alignas(void*) std::array<std::byte,0x80> video{};
        for(const auto offset:{0x48,0x4c,0x50}) *reinterpret_cast<int*>(video.data()+offset)=1;
        eu4unicode::original_ime_message=native_handler;
        LPARAM lifecycle=0;
        require(eu4unicode::show_native_ime_candidates(nullptr,WM_IME_STARTCOMPOSITION,0,&lifecycle,video.data())==1);
        require(eu4unicode::native_composition().active);
        require(eu4unicode::show_native_ime_candidates(nullptr,WM_IME_ENDCOMPOSITION,0,&lifecycle,video.data())==1);
        require(!eu4unicode::native_composition().active);
        eu4unicode::show_native_ime_candidates(nullptr,WM_IME_STARTCOMPOSITION,0,&lifecycle,video.data());
        eu4unicode::show_native_ime_candidates(nullptr,WM_KILLFOCUS,0,&lifecycle,video.data());
        require(!eu4unicode::native_composition().active);
        for(const LPARAM incoming:{LPARAM{0},LPARAM{1},LPARAM{0xf},LPARAM{0xc000000f}}) {
            auto flags=incoming;const auto before=calls;
            require(eu4unicode::show_native_ime_candidates(nullptr,WM_IME_SETCONTEXT,1,&flags,video.data())==0);
            require(flags==incoming&&calls==before+1);
        }
        for(const auto event:{IMN_OPENCANDIDATE,IMN_CHANGECANDIDATE,IMN_CLOSECANDIDATE}) {
            LPARAM flags=3;const auto before=calls;
            require(eu4unicode::show_native_ime_candidates(nullptr,WM_IME_NOTIFY,event,&flags,video.data())==0);
            require(flags==3&&calls==before+1);
        }
        for(const auto event:{IMN_SETOPENSTATUS,IMN_SETCONVERSIONMODE,IMN_PRIVATE}) {
            LPARAM flags=17;
            require(eu4unicode::show_native_ime_candidates(nullptr,WM_IME_NOTIFY,event,&flags,video.data())==1&&flags==17);
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
        std::puts("PASS: native IME UI flags, lifecycle propagation, disabled contexts and single commit dispatch.");
    } catch(const std::exception& error) { std::fprintf(stderr,"%s\n",error.what());return 1; }
}
