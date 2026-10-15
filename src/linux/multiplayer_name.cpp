#include "linux/native.hpp"
#include "unicode_text.hpp"
#include <atomic>
#include <string>

namespace eu4unicode::linux_native {
namespace {
std::atomic<std::uint64_t> checked{0};
void controller_characters(safetyhook::Context& context) {
    auto* name=reinterpret_cast<String*>(context.rdi);
    const auto text=name->view();
    if(!valid_utf8(text)) return;
    std::string cleaned;
    cleaned.reserve(text.size());
    auto remaining=text;
    while(!remaining.empty()) {
        const auto unit=decode(remaining);
        switch(unit.value) {
        case '@': cleaned.push_back('a');break;
        case '$': cleaned.push_back('S');break;
        case 0xa7: cleaned.push_back('s');break;
        default: cleaned.append(remaining.data(),unit.bytes);break;
        }
        remaining.remove_prefix(unit.bytes);
    }
    if(cleaned!=text) {
        // The stack CString is already constructed. Its native assignment
        // retains GNU string ownership and updates its byte length.
        reinterpret_cast<void(*)(void*,std::size_t,std::size_t,const char*,std::size_t)>(0x25a3f30)(
            name,0,text.size(),cleaned.data(),cleaned.size());
    }
    context.rip=0x1c6e74c;
    if(checked.fetch_add(1,std::memory_order_relaxed)==0)
        log("Multiplayer name UTF-8 sanitization applied");
}
void special_characters(safetyhook::Context& context) {
    // Only this call in CFrontendMultiplayerView::Show is changed. The native
    // Latin-1 transliterator would corrupt each byte of a UTF-8 name.
    const auto* name=reinterpret_cast<const String*>(context.rdi);
    if(valid_utf8(name->view())) context.rip=0x1c6e756;
}
}
void prepare_multiplayer_name(Hooks& hooks) {
    verify_site(0x25a3f30);
    hooks.add_mid(0x1c6e747,controller_characters);
    hooks.add_mid(0x1c6e751,special_characters);
}
}
