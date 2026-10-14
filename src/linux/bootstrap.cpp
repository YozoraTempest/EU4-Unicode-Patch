#include "linux/native.hpp"
#include <dlfcn.h>
#include <elf.h>
#include <openssl/evp.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <unistd.h>

namespace eu4unicode::linux_native {
namespace {
using Main=int(*)(int,char**,char**);
Main game_main=nullptr;
// The plugin and trampolines live until process exit. Teardown runs after the
// engine has finished; no unhooking races with native worker threads.
Hooks* hooks=nullptr;
FILE* logfile=nullptr;
int patched_main(int argc,char** argv,char** envp) {
    if(!verify_executable()) return 1;
    try {
        verify_profile();
        hooks=new Hooks;
        prepare_localization(*hooks);
        prepare_bitmap_fonts(*hooks);
        prepare_mod_registry(*hooks);
        prepare_editor_input(*hooks);
        hooks->enable();
        log("Linux native hooks enabled; profile=EU4-1.37.5-x86_64");
    } catch(const std::exception& error) {
        log("Refused: %s",error.what());
        if(hooks) hooks->clear();
        return 1;
    }
    const int result=game_main(argc,argv,envp);
    report_text_counts();
    report_input_counts();
    return result;
}
}
void log(const char* format, ...) noexcept {
    if(!logfile) logfile=std::fopen("eu4_unicode_linux.log","a");
    va_list args;
    va_start(args,format);
    if(logfile) {
        std::fputs("[EU4UnicodePatch] ",logfile);
        std::vfprintf(logfile,format,args);
        std::fputc('\n',logfile);
        std::fflush(logfile);
    }
    va_end(args);
}
bool verify_executable() {
    std::ifstream file("/proc/self/exe",std::ios::binary);
    Elf64_Ehdr header{};
    file.read(reinterpret_cast<char*>(&header),sizeof(header));
    if(!file||std::memcmp(header.e_ident,ELFMAG,SELFMAG)||
       header.e_ident[EI_CLASS]!=ELFCLASS64||header.e_machine!=EM_X86_64||
       header.e_type!=ET_EXEC) {log("Refused: unsupported ELF ABI");return false;}
    file.seekg(0);
    const auto context=std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)>(
        EVP_MD_CTX_new(),EVP_MD_CTX_free);
    if(!context||EVP_DigestInit_ex(context.get(),EVP_sha256(),nullptr)!=1) return false;
    std::array<char,65536> bytes{};
    while(file.read(bytes.data(),bytes.size())||file.gcount())
        if(EVP_DigestUpdate(context.get(),bytes.data(),file.gcount())!=1) return false;
    unsigned char digest[EVP_MAX_MD_SIZE];unsigned int size=0;
    if(!file.eof()||EVP_DigestFinal_ex(context.get(),digest,&size)!=1||size!=32) return false;
    char hash[65]{};
    for(unsigned int i=0;i<size;++i) std::snprintf(hash+i*2,3,"%02x",digest[i]);
    log("Executable SHA256=%s",hash);
    if(std::strcmp(hash,"af115d3b0e54a05eca0198ed569db90ca225728afda03b5ac4ded251520a7ce3")) {
        log("Refused: unsupported executable");return false;
    }
    // SafetyHook's Linux backend does not suspend other threads. Install all
    // hooks before main, and refuse a preloader that has already started workers.
    unsigned int threads=0;
    std::ifstream status("/proc/self/status");
    std::string line;
    while(std::getline(status,line)) if(line.starts_with("Threads:"))
        threads=static_cast<unsigned int>(std::stoul(line.substr(8)));
    if(threads!=1) {log("Refused: startup has %u threads",threads);return false;}
    return true;
}
SafetyHookInline& Hooks::add_inline(std::uintptr_t address,void* destination) {
    verify_site(address);
    auto result=SafetyHookInline::create(reinterpret_cast<void*>(address),destination,
                                         SafetyHookInline::StartDisabled);
    if(!result) throw std::runtime_error("Inline hook preparation failed at "+std::to_string(address));
    inline_.push_back(std::make_unique<SafetyHookInline>(std::move(*result)));
    return *inline_.back();
}
void Hooks::add_mid(std::uintptr_t address,safetyhook::MidHookFn destination) {
    verify_site(address);
    auto result=SafetyHookMid::create(reinterpret_cast<void*>(address),destination,
                                     SafetyHookMid::StartDisabled);
    if(!result) throw std::runtime_error("Mid hook preparation failed at "+std::to_string(address));
    mid_.push_back(std::move(*result));
}
void Hooks::enable() {
    for(auto& hook:inline_) if(!hook->enable()) throw std::runtime_error("Inline hook installation failed");
    for(auto& hook:mid_) if(!hook.enable()) throw std::runtime_error("Mid hook installation failed");
    log("Installed inline=%zu mid=%zu",inline_.size(),mid_.size());
}
void Hooks::clear() noexcept {mid_.clear();inline_.clear();}
}

// The Steam runtime wrapper also launches shell/helper executables. Interpose
// only the game's main; other programs are forwarded without touching memory.
extern "C" int __libc_start_main(int(*main)(int,char**,char**),int argc,char** argv,
    void(*init)(),void(*fini)(),void(*rtld_fini)(),void* stack_end) {
    using Start=int(*)(int(*)(int,char**,char**),int,char**,void(*)(),void(*)(),void(*)(),void*);
    const auto original=reinterpret_cast<Start>(dlsym(RTLD_NEXT,"__libc_start_main"));
    if(!original) _exit(127);
    char path[4096]{};
    const auto length=readlink("/proc/self/exe",path,sizeof(path)-1);
    const char* basename=length>0?std::strrchr(path,'/'):nullptr;
    if(basename&&std::strcmp(basename+1,"eu4")==0) {
        eu4unicode::linux_native::game_main=main;
        main=eu4unicode::linux_native::patched_main;
    }
    return original(main,argc,argv,init,fini,rtld_fini,stack_end);
}
