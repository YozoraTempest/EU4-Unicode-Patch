#define EU4_UNICODE_RENDER_IMPLEMENTATION
#include "linux/render_api.hpp"
#include <dlfcn.h>
#include <stdexcept>
#include <string>

namespace eu4unicode::linux_native {
RenderApi::RenderApi() {
    void* handle=dlopen("libpangocairo-1.0.so.0",RTLD_NOW|RTLD_LOCAL|RTLD_DEEPBIND);
    if(!handle) {
        const auto error=dlerror();
        throw std::runtime_error(std::string("Cannot load Unicode rendering runtime: ")+(error?error:"unknown error"));
    }
    // Keep this scope alive until exit; retained glyph faces can outlive
    // layouts and must never call into an unloaded font or allocation API.
#define EU4_RENDER_RESOLVE(name) \
    name=reinterpret_cast<decltype(name)>(dlsym(handle,#name)); \
    if(!name) throw std::runtime_error("Unicode rendering runtime is missing " #name " (Pango 1.46 or later is required)");
    EU4_UNICODE_RENDER_SYMBOLS(EU4_RENDER_RESOLVE)
#undef EU4_RENDER_RESOLVE
    // Sniper's Pango 1.46 has a single baseline per line. Pango 1.50 added
    // per-run font baseline adjustments; retain them on newer runtimes used
    // by the existing generic Linux package.
    pango_layout_iter_get_run_baseline=reinterpret_cast<decltype(pango_layout_iter_get_run_baseline)>(
        dlsym(handle,"pango_layout_iter_get_run_baseline"));
    if(!pango_layout_iter_get_run_baseline) pango_layout_iter_get_run_baseline=pango_layout_iter_get_baseline;
}

RenderApi& render_api() {
    static auto* api=new RenderApi;
    return *api;
}
}
