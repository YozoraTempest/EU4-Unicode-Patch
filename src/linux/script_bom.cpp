#include "linux/native.hpp"
#include <array>
#include <atomic>
#include <cstring>

namespace eu4unicode::linux_native {
namespace {
SafetyHookInline *file_hook=nullptr,*mode_hook=nullptr,*stream_hook=nullptr;
std::atomic<unsigned long long> consumed{0};
void skip_signature(void* lexer) {
    void* input=nullptr;
    std::memcpy(&input,static_cast<char*>(lexer)+8,sizeof(input));
    if(!input) return;
    const auto table=*static_cast<void***>(input);
    // Verified GNU CFile ABI: two destructor entries and a native IsValid
    // entry put SeekStart/GetPosition one slot beyond the Windows layout.
    const auto position=reinterpret_cast<std::int64_t(*)(void*)>(table[0x88/8]);
    if(position(input)!=0) return;
    std::array<unsigned char,3> header{};
    const auto read=reinterpret_cast<bool(*)(void*,void*,int)>(table[0x20/8]);
    const bool complete=read(input,header.data(),static_cast<int>(header.size()));
    if(complete&&header==std::array<unsigned char,3>{0xef,0xbb,0xbf}) {
        consumed.fetch_add(1,std::memory_order_relaxed);return;
    }
    // Restore every non-signature probe, including a short/failed stream read.
    // The memory stream is all-or-nothing; other CFile implementations may
    // advance their cursor before reporting an incomplete read.
    reinterpret_cast<bool(*)(void*,int)>(table[0x68/8])(input,0);
}
void file(void* lexer,String* path) {
    file_hook->call<void>(lexer,path);skip_signature(lexer);
}
void mode(void* lexer,String* path,int type) {
    mode_hook->call<void>(lexer,path,type);skip_signature(lexer);
}
void stream(void* lexer,void* input,bool own_input) {
    stream_hook->call<void>(lexer,input,own_input);skip_signature(lexer);
}
}
void prepare_script_bom(Hooks& hooks) {
    file_hook=&hooks.add_inline(0x255e290,reinterpret_cast<void*>(file));
    mode_hook=&hooks.add_inline(0x255e32c,reinterpret_cast<void*>(mode));
    stream_hook=&hooks.add_inline(0x255e3c8,reinterpret_cast<void*>(stream));
}
void report_script_counts() noexcept {
    log("Native script UTF-8 signatures consumed=%llu",consumed.load());
}
}
