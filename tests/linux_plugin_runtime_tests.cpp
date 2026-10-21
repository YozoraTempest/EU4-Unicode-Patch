#include "linux/plugin_runtime.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void check(bool value,const char* message) {
    if(!value) throw std::runtime_error(message);
}
std::string environment(const char* key) {
    const auto value=std::getenv(key);
    return value?value:"";
}
}
int main() {
    char temporary[]="/tmp/eu4-unicode-runtime-XXXXXX";
    check(mkdtemp(temporary)!=nullptr,"Cannot create test directory");
    const std::filesystem::path directory=temporary;
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::filesystem::remove_all(path);}} cleanup{directory};
    std::filesystem::create_directories(directory/"lib");
    std::ofstream(directory/"lib/libSDL2-2.0.so.0") << "input-runtime-fixture";
    for(const auto locale:{"C","POSIX","C.UTF-8","zh_CN.UTF-8"}) {
        setenv("LC_ALL",locale,1);
        unsetenv("SDL_DYNAMIC_API");
        eu4unicode::linux_native::prepare_environment(directory);
        check(environment("SDL_DYNAMIC_API")==directory/"lib/libSDL2-2.0.so.0","Bundled SDL must be selected");
        const std::string expected=std::string(locale)=="C"||std::string(locale)=="POSIX"?"C.UTF-8":locale;
        check(environment("LC_ALL")==expected,"Locale normalization must preserve explicit UTF-8 locales");
    }
    setenv("SDL_DYNAMIC_API","/player/custom/SDL.so",1);
    eu4unicode::linux_native::prepare_environment(directory);
    check(environment("SDL_DYNAMIC_API")=="/player/custom/SDL.so","Explicit SDL choice must be preserved");
    std::cout << "Plugin input environment checks passed\n";
}
