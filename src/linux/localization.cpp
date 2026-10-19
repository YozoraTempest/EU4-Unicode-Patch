#include "linux/native.hpp"
#include "legacy_text.hpp"
#include "unicode_text.hpp"
#include <atomic>
#include <vector>

namespace eu4unicode::linux_native {
namespace {
struct Context {
    int line;
    bool replace;
    char padding[11];
    void* collection;
};
static_assert(offsetof(Context,collection)==16);
using Register=void(*)(void*,const char*,const char*,int,int,bool);
std::atomic<std::uint64_t> imported{0},rejected{0};
SafetyHookInline* yaml_hook=nullptr;
std::atomic<std::uint64_t> yaml_signatures{0};
struct YamlBytes {const unsigned char* begin;const unsigned char* end;const unsigned char* capacity;};
using LanguageCallback=void(*)(const String*,int,void*);
using KeyCallback=void(*)(const String*,const String*,int,int,void*);
void parse_yaml(void* file,const YamlBytes* input,const char* name,LanguageCallback language,KeyCallback key,void* context) {
    const auto size=input->begin?static_cast<std::size_t>(input->end-input->begin):0;
    const auto bytes=std::string_view(reinterpret_cast<const char*>(input->begin),size);
    if(size&&bytes.substr(0,3)!="\xef\xbb\xbf"&&valid_utf8(bytes)) {
        // Normalize only the parse buffer; source files and string ownership
        // stay native. The engine itself still consumes its required signature.
        std::vector<unsigned char> normalized{0xef,0xbb,0xbf};
        normalized.insert(normalized.end(),input->begin,input->end);
        const YamlBytes buffer{normalized.data(),normalized.data()+normalized.size(),normalized.data()+normalized.size()};
        ++yaml_signatures;
        yaml_hook->call<void>(file,&buffer,name,language,key,context);
    } else yaml_hook->call<void>(file,input,name,language,key,context);
}
void import(const String* key,const String* value,int version,int,const Context* context) noexcept {
    try {
        const auto text=value->view();
        const auto native=reinterpret_cast<Register>(0x236b7f8);
        if(contains_legacy_escape(text)) {
            const auto decoded=decode_legacy_text(text,LegacyPayload::cp1252_in_utf8);
            if(decoded.error!=LegacyError::none) {
                if(rejected.fetch_add(1)<10) log("Localization rejected key=%s line=%d error=%s",
                    key->bytes,context->line,legacy_error_name(decoded.error));
                return;
            }
            native(context->collection,key->bytes,decoded.text.c_str(),context->line,version,context->replace);
        } else if(valid_utf8(text)) {
            native(context->collection,key->bytes,value->bytes,context->line,version,context->replace);
        } else {
            if(rejected.fetch_add(1)<10) log("Localization rejected key=%s line=%d error=invalid-utf8",
                key->bytes,context->line);
            return;
        }
        imported.fetch_add(1,std::memory_order_relaxed);
    } catch(...) {rejected.fetch_add(1,std::memory_order_relaxed);}
}
}
void prepare_localization(Hooks& hooks) {
    verify_site(0x236b7f8);
    hooks.add_inline(0x23671a1,reinterpret_cast<void*>(import));
    yaml_hook=&hooks.add_inline(0x2366224,reinterpret_cast<void*>(parse_yaml));
}
void report_text_counts() noexcept {
    log("Localization imported=%llu rejected=%llu",
        static_cast<unsigned long long>(imported.load()),static_cast<unsigned long long>(rejected.load()));
    log("Native YAML UTF-8 signatures supplied=%llu",static_cast<unsigned long long>(yaml_signatures.load()));
}
}
