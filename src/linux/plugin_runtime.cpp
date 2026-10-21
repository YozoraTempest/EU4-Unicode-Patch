#include "linux/plugin_runtime.hpp"
#include <dlfcn.h>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

namespace eu4unicode::linux_native {
std::filesystem::path plugin_directory() {
    Dl_info info{};
    if(!dladdr(reinterpret_cast<void*>(&plugin_directory),&info)||!info.dli_fname)
        throw std::runtime_error("Cannot locate Unicode plugin resources");
    return std::filesystem::canonical(info.dli_fname).parent_path();
}

std::filesystem::path resource_directory() {
    const auto directory=plugin_directory();
    const auto resources=directory/"eu4_unicode";
    return std::filesystem::is_directory(resources)?resources:directory;
}

void prepare_environment(const std::filesystem::path& directory) {
    const auto locale=std::getenv("LC_ALL");
    if(locale&&(std::string_view(locale)=="C"||std::string_view(locale)=="POSIX")) {
        if(setenv("LC_ALL","C.UTF-8",1)!=0)
            throw std::runtime_error("Cannot configure UTF-8 input locale");
    }
    const auto configured=std::getenv("SDL_DYNAMIC_API");
    if(configured&&*configured) return;
    for(const auto& candidate:{directory/"lib/libSDL2-2.0.so.0",
        std::filesystem::path("/usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0"),
        std::filesystem::path("/usr/lib64/libSDL2-2.0.so.0"),
        std::filesystem::path("/usr/lib/libSDL2-2.0.so.0")}) {
        if(std::filesystem::is_regular_file(candidate)) {
            if(setenv("SDL_DYNAMIC_API",candidate.c_str(),1)!=0)
                throw std::runtime_error("Cannot configure SDL input runtime");
            return;
        }
    }
    throw std::runtime_error("SDL2 input runtime is missing; run the plugin in Steam Linux Runtime 3.0 (sniper)");
}
}
