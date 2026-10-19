#include "linux/script_legacy.hpp"
#include "legacy_text.hpp"
#include <atomic>
#include <cstring>
namespace eu4unicode::linux_native {
namespace {
SafetyHookInline* token_hook=nullptr;
std::atomic<unsigned long long> imported{0},sequences{0},rejected{0};
int token(ScriptLexer* lexer) {
    const auto result=token_hook->call<int>(lexer);
    return result&&import_script_token(*lexer)?result:0;
}
}
bool import_script_token(ScriptLexer& lexer) noexcept {
    try {
        const auto result=import_legacy_script_buffer(lexer.text,sizeof(lexer.text),lexer.token_type);
        if(result.error!=LegacyError::none) {
            int line=0;
            if(lexer.input) std::memcpy(&line,static_cast<const char*>(lexer.input)+8,sizeof(line));
            if(rejected.fetch_add(1)<10) log("Script legacy rejected line=%d offset=%zu error=%s",
                line,result.error_offset,legacy_error_name(result.error));
            return false;
        }
        if(result.sequences) {++imported;sequences.fetch_add(result.sequences);}
        return true;
    } catch(...) {lexer.text[0]=0;++rejected;return false;}
}
void prepare_script_legacy(Hooks& hooks) {
    token_hook=&hooks.add_inline(0x255e44e,reinterpret_cast<void*>(token));
}
void report_script_legacy_counts() noexcept {
    log("Native legacy script tokens=%llu sequences=%llu rejected=%llu",imported.load(),sequences.load(),rejected.load());
}
}
