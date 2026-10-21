#include "native_legacy_import.hpp"
#include "legacy_text.hpp"
#include "unicode_text.hpp"
#include <atomic>
#include <cstring>

namespace eu4unicode {
NativeTextRegistration register_imported_text=nullptr;
NativeScriptTokenRead original_script_token=nullptr;
NativeScriptTokenRead original_binary_token=nullptr;
LegacyImportDiagnostic legacy_import_diagnostic=nullptr;
namespace {
std::atomic<std::uint64_t> localization{0},script{0},binary{0},sequences{0},relocated{0},rejected{0};
void diagnose(std::string_view source,int line,std::string_view key,std::size_t offset,const char* error) {
    rejected.fetch_add(1,std::memory_order_relaxed);
    if(legacy_import_diagnostic) legacy_import_diagnostic(source,line,key,offset,error);
}
void record(const LegacyText& decoded,std::atomic<std::uint64_t>& entries) {
    entries.fetch_add(1,std::memory_order_relaxed);
    sequences.fetch_add(decoded.sequences,std::memory_order_relaxed);
    relocated.fetch_add(decoded.relocated,std::memory_order_relaxed);
}
struct ScriptInputContext {
    void** vtable;
    int line;
    unsigned char last,pushback;
    char padding[10];
    EngineString filename;
};
static_assert(offsetof(ScriptInputContext,filename)==0x18);
void script_error(NativeScriptLexer* lexer,std::size_t offset,const char* error,const char* kind="script token") {
    const auto input=static_cast<const ScriptInputContext*>(lexer->input);
    const auto filename=input?std::string_view(input->filename.data(),static_cast<std::size_t>(input->filename.size)):
        std::string_view("script");
    diagnose(filename,input?input->line:0,kind,offset,error);
}
}
LegacyImportCounts legacy_import_counts() noexcept {
    return {localization.load(std::memory_order_relaxed),script.load(std::memory_order_relaxed),
            binary.load(std::memory_order_relaxed),
            sequences.load(std::memory_order_relaxed),relocated.load(std::memory_order_relaxed),
            rejected.load(std::memory_order_relaxed)};
}
void import_legacy_localization(const EngineString* key,const EngineString* value,
                               int version,int,const NativeLocalizationContext* context) noexcept {
    const auto key_text=std::string_view(key->data(),static_cast<std::size_t>(key->size));
    const auto text=std::string_view(value->data(),static_cast<std::size_t>(value->size));
    try {
        if(contains_legacy_escape(text)) {
            const auto decoded=decode_legacy_text(text,LegacyPayload::cp1252_in_utf8);
            if(decoded.error!=LegacyError::none) {
                diagnose("localization",context->line,key_text,decoded.error_offset,legacy_error_name(decoded.error));return;
            }
            // The native registration routine copies both C strings into its
            // collection; this buffer is borrowed only for the duration of it.
            register_imported_text(context->collection,key->data(),decoded.text.c_str(),
                                   context->line,version,context->replace);
            record(decoded,localization);
        } else if(valid_utf8(text)) {
            register_imported_text(context->collection,key->data(),value->data(),
                                   context->line,version,context->replace);
        } else diagnose("localization",context->line,key_text,0,"invalid UTF-8");
    } catch(...) {diagnose("localization",context->line,key_text,0,"text import failed");}
}
bool convert_legacy_script_token(NativeScriptLexer* lexer) noexcept {
    try {
        const auto decoded=import_legacy_script_buffer(lexer->text,sizeof(lexer->text),lexer->token_type);
        if(decoded.error!=LegacyError::none) {
            script_error(lexer,decoded.error_offset,legacy_error_name(decoded.error));lexer->text[0]=0;return false;
        }
        if(decoded.sequences) record(decoded,script);
        return true;
    } catch(...) {script_error(lexer,0,"text import failed");lexer->text[0]=0;return false;}
}
bool convert_legacy_binary_token(NativeScriptLexer* lexer) noexcept {
    try {
        const auto decoded=import_legacy_binary_buffer(lexer->text,sizeof(lexer->text),lexer->token_type);
        if(decoded.error!=LegacyError::none) {
            script_error(lexer,decoded.error_offset,legacy_error_name(decoded.error),"binary token");return false;
        }
        if(decoded.sequences) record(decoded,binary);
        return true;
    } catch(...) {
        script_error(lexer,0,"text import failed","binary token");lexer->text[0]=0;return false;
    }
}
int read_legacy_binary_token(NativeScriptLexer* lexer) {
    const auto result=original_binary_token(lexer);
    if(!result||!convert_legacy_binary_token(lexer)) return 0;
    return result;
}
int read_legacy_script_token(NativeScriptLexer* lexer) {
    const auto result=original_script_token(lexer);
    if(!result||!convert_legacy_script_token(lexer)) return 0;
    return result;
}
}
