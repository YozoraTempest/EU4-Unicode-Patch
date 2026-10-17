#include "linux/native.hpp"
#include "legacy_text.hpp"
#include "unicode_text.hpp"
#include <atomic>

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
}
void report_text_counts() noexcept {
    log("Localization imported=%llu rejected=%llu",
        static_cast<unsigned long long>(imported.load()),static_cast<unsigned long long>(rejected.load()));
}
}
