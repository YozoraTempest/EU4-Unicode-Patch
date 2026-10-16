#include "linux/editor_mutation.hpp"
#include "formatted_text.hpp"
#include "unicode_text.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <stdexcept>

namespace eu4unicode::linux_native {
std::string filter_editor_input(std::string_view text,std::string_view forbidden) {
    if(!valid_utf8(text)) throw std::invalid_argument("Invalid UTF-8 editor input");
    std::array<bool,256> excluded{};
    for(const unsigned char byte:forbidden) excluded[byte]=true;
    std::string result;
    result.reserve(text.size());
    for(std::size_t offset=0;offset<text.size();) {
        const auto scalar=decode(text.substr(offset));
        if(scalar.value>255||!excluded[scalar.value]) result.append(text.substr(offset,scalar.bytes));
        offset+=scalar.bytes;
    }
    return result;
}
std::size_t editor_prefix(std::string_view text,std::size_t limit,bool formatted) {
    if(!valid_utf8(text)) throw std::invalid_argument("Invalid UTF-8 editor document");
    return FormattedText(text,formatted).prefix(limit);
}
namespace {
std::atomic<std::uint64_t> filters{0},clamps{0};
void filter(safetyhook::Context& context) {
    // Only CTextBuffer::Write's RemoveIf call is intercepted. Both scratch
    // CStrings are already owned by the engine and retain native cleanup.
    auto text=reinterpret_cast<String*>(context.rdi);
    const auto forbidden=reinterpret_cast<const String*>(context.rsi);
    const auto source=text->view();
    if(!valid_utf8(source)) return;
    try {
        const auto result=filter_editor_input(source,forbidden->view());
        const bool changed=result!=source;
        if(changed) reinterpret_cast<void(*)(String*,std::size_t,std::size_t,const char*,std::size_t)>(0x25a3f30)(
            text,0,source.size(),result.data(),result.size());
        context.rax=changed;
        context.rip=0x20d5ce8;
        const auto count=filters.fetch_add(1)+1;
        if(count<=4) log("Linux editor scalar filter input-bytes=%zu output-bytes=%zu",source.size(),result.size());
    } catch(...) {
        // Keep the document intact on failure, then run native scratch cleanup.
        context.rip=0x20d5e75;
        log("Linux editor input rejected: scalar filtering failed");
    }
}
void clamp(safetyhook::Context& context) {
    const auto limit=static_cast<std::int32_t>(context.rcx);
    const auto text=reinterpret_cast<const String*>(context.rsi);
    if(limit<0||static_cast<std::size_t>(limit)>=text->size||!valid_utf8(text->view())) return;
    // All five verified calls pass the document at CTextBuffer+0x30. Only
    // their end index changes; construction, assignment and destruction stay
    // native, and max-chars remains a byte capacity rather than a scalar count.
    bool formatted;
    std::memcpy(&formatted,reinterpret_cast<const char*>(text)+0xe4-0x30,sizeof(formatted));
    try {
        const auto end=editor_prefix(text->view(),static_cast<std::size_t>(limit),formatted);
        if(end!=static_cast<std::size_t>(limit)) {
            context.rcx=end;
            const auto count=clamps.fetch_add(1)+1;
            if(count<=4) log("Linux editor byte limit aligned from=%d to=%zu",limit,end);
        }
    } catch(...) {
        log("Linux editor byte limit rejected: boundary query failed");
    }
}
}
void prepare_editor_mutation(Hooks& hooks) {
    for(const auto address:{0x25a3f30,0x20d5ce8,0x20d5e75}) verify_site(address);
    hooks.add_mid(0x20d5ce3,filter);
    for(const auto address:{0x20d61e4,0x20d622d,0x20d6722,0x20d676b,0x20d68f5})
        hooks.add_mid(address,clamp);
}
void report_editor_mutation_counts() noexcept {
    log("Linux editor scalar-filters=%llu boundary-clamps=%llu",
        static_cast<unsigned long long>(filters.load()),static_cast<unsigned long long>(clamps.load()));
}
}
