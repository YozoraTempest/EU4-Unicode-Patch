#include "unicode_text.hpp"
#include "native_text_event.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

void check(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    using namespace eu4unicode;
    const std::string mixed=u8"A中文 é 日本語 한국어 𠀀😀";
    const std::vector<std::uint32_t> expected={0x41,0x4e2d,0x6587,0x20,0xe9,
        0x20,0x65e5,0x672c,0x8a9e,0x20,0xd55c,0xad6d,0xc5b4,0x20,0x20000,0x1f600};
    auto remaining=std::string_view(mixed);
    for(auto cp:expected) {
        auto s=decode(remaining);
        check(s.valid && s.value==cp,"mixed-language scalar decoding");
        remaining.remove_prefix(s.bytes);
    }
    check(remaining.empty(),"consume entire mixed text");
    check(valid_utf8(mixed),"valid mixed UTF-8");
    for (std::size_t limit = 0; limit <= mixed.size(); ++limit) {
        const auto end = scalar_prefix(mixed, limit);
        check(end <= limit && valid_utf8(std::string_view(mixed).substr(0, end)),
            "bounded buffers retain only complete UTF-8 scalars");
        check(end == mixed.size() || end + decode(std::string_view(mixed).substr(end)).bytes > limit,
            "bounded prefix uses all available space");
    }
    check(scalar_prefix(std::string(31999, 'A') + u8"𠀀Z", 32000) == 31999,
        "engine text limit does not cut a supplementary scalar");
    for(const auto& bad:std::vector<std::string>{"\xc0\xaf","\xed\xa0\x80",
        "\xf4\x90\x80\x80","\xf0\x9f","\xe4\xb8","\x80"}) {
        check(!valid_utf8(bad),"reject invalid encoding");
        check(!decode(bad).valid && decode(bad).bytes==1,"invalid input advances safely");
    }
    check(decode({}).bytes==0,"empty input");
    const std::string highest="\xf4\x8f\xbf\xbf";
    check(decode(highest).value==0x10ffff,"highest Unicode scalar retained");
    check(bitmap_slot(0x4e2d)==0x4e2d,"Chinese glyph mapping");
    check(bitmap_slot(0x3b1)==0x3b1,"glyph key preserves the original Unicode scalar");
    check(bitmap_slot(0x20000)==0x20000 && decode(u8"𠀀").value==0x20000,
        "supplementary glyph keys retain the complete code point");
    check(bitmap_slot(0x2014)==0x2014,"native Unicode punctuation glyph ID");
    NativeTextEvent event{};
    const std::string commit=u8"A👩‍👩‍👧‍👦𠀀";
    check(make_text_event(commit,event),"queue accepts a complete 30-byte commit");
    NativeTextEvent copied=event;
    event={};
    check(queued_utf8(copied)==commit,"queued payload survives source lifetime and complete node copies");
    check(make_text_event(std::string(27,'A')+u8"𠀀",event),"31-byte SDL payload fits");
    check(!make_text_event(std::string(28,'A')+u8"𠀀",event)&&queued_utf8(event).empty(),"oversize payload leaves no marked event");
    check(!make_text_event("\xf0\x9f",event),"truncated input never enters the native queue");
    check(!make_text_event(std::string("A\0Z",3),event),"embedded NUL is rejected");
    check(!make_text_event({},event),"empty SDL input is ignored");
    make_text_event(u8"👧",event);event.text[3]='\0';
    check(queued_utf8(event).empty(),"corrupted copied payload is rejected");
    make_text_event(u8"𠀀",event);event.utf8_tag=0;
    check(queued_utf8(event).empty(),"native one-byte events remain unmarked");
    make_text_event(u8"𠀀",event);event.type=1;
    check(queued_utf8(event).empty(),"keyboard events cannot become text commits");
    std::cout << "Unicode scalar, invalid-input and bitmap-adapter checks passed.\n";
}
