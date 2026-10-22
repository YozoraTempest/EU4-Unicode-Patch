#include "linux/native.hpp"
#include "unicode_text.hpp"

namespace eu4unicode::linux_native {
namespace {
template<std::uintptr_t Next>
void preserve_utf8(safetyhook::Context& context) {
    const auto* name=reinterpret_cast<const String*>(context.rdi);
    // Only verified save-name/path calls bypass the byte-wise Latin-1
    // transliterator. Filename validation and non-UTF-8 callers stay native.
    if(valid_utf8(name->view())) context.rip=Next;
}
}
void prepare_save_names(Hooks& hooks) {
    hooks.add_mid(0x1cdcbfb,preserve_utf8<0x1cdcc00>); // CIngameSaveMenu::SaveGame
    hooks.add_mid(0x1cdd249,preserve_utf8<0x1cdd24e>); // CIngameSaveMenu::SaveGameSelect
    hooks.add_mid(0x1cdd63e,preserve_utf8<0x1cdd643>); // CIngameSaveMenu::Show (default name)
    hooks.add_mid(0x1cdddc9,preserve_utf8<0x1cdddce>); // CIngameLoadMenu::SaveGameSelect
    hooks.add_mid(0x12833cb,preserve_utf8<0x12833d0>); // EU4SaveGameHelper::BuildFullSavePath
}
}
