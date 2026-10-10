#include "formatted_text.hpp"
#include "formatted_text_cache.hpp"
#include "unicode_text.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace eu4unicode;
void check(bool value,const char* message) {
    if(!value) throw std::runtime_error(message);
}
void check_cache() {
    FormattedTextCache cache(1024*1024,2);
    std::string source=u8"中e§R\u0301§!文";
    const auto original=source;
    const auto first=cache.get(source);
    source.assign("changed");
    check(cache.get(original)==first,"cache key borrowed mutable caller storage");
    const auto second=cache.get(u8"§Y省份§!");
    check(cache.get(original)==first,"recent boundary value was replaced");
    cache.get(u8"第三条文字");
    check(cache.size()==2&&cache.get(original)==first,"least recent entry eviction removed hot text");
    check(cache.get(u8"§Y省份§!")!=second,"least recent entry was not evicted");
    check(second->visible_text()==u8"省份","eviction invalidated an active layout value");
    const FormattedText expected(original);
    for(std::size_t offset=0;offset<=original.size();++offset) {
        check(first->prefix(offset)==expected.prefix(offset),"cached prefix changed formatted clusters");
        check(first->line_before(offset)==expected.line_before(offset),"cached line boundary changed formatted clusters");
    }

    FormattedTextCache large;
    std::vector<std::shared_ptr<const FormattedText>> values;
    for(int index=0;index<600;++index)
        values.push_back(large.get(u8"§Y中文地名§! £adm£ "+std::to_string(index)));
    check(large.size()==600,"ordinary UI working set exceeded cache limits");
    for(int index=0;index<600;++index)
        check(large.get(u8"§Y中文地名§! £adm£ "+std::to_string(index))==values[index],
              "repeated large UI working set rebuilt boundaries");
    for(int index=0;index<2400;++index) {
        large.get(u8"变化文字"+std::to_string(index));
        check(large.get(u8"§Y中文地名§! £adm£ 0")==values[0],"changing text evicted a frequently used layout");
    }
    check(large.size()<=1024&&large.cached_bytes()<=1024*1024,"cache exceeded retention limits");

    FormattedTextCache formats;
    check(formats.get(u8"§Y中文§!")!=formats.get(u8"§R中文§!"),"distinct format commands share byte offsets");
    check(formats.get("@FRA")!=formats.get("@FRA "),"distinct flag source lengths share a cache key");
    check(formats.get("") == formats.get(""),"empty text cannot be cached");

    FormattedTextCache measured;
    const auto retained=measured.get("first");
    const auto one_entry=measured.cached_bytes();
    check(one_entry>0,"cache retention accounting is empty");
    FormattedTextCache limited(one_entry*2,10);
    const auto hot=limited.get("first");
    const auto cold=limited.get("other");
    limited.get("first");
    limited.get("third");
    check(limited.size()==2&&limited.cached_bytes()<=one_entry*2,"byte limit did not evict one old entry");
    check(limited.get("first")==hot,"byte pressure removed recently used text");
    check(limited.get("other")!=cold,"byte pressure retained the least recent entry");
    limited.get("first");
    const auto before=limited.cached_bytes();
    const auto oversized=limited.get(std::string(20000,'x'));
    check(oversized->visible_text().size()==20000,"uncached large text lost its layout");
    check(limited.cached_bytes()==before&&limited.get("first")==hot,"oversized text flushed ordinary UI entries");
    FormattedTextCache disabled_bytes(0,10),disabled_entries(1024,0);
    disabled_bytes.get("first");disabled_entries.get("first");
    check(disabled_bytes.size()==0&&disabled_entries.size()==0,"disabled cache retained values");
    check(retained->visible_text()=="first","independent cache lifetime changed a retained value");
}
int main() {
    try {
        check_cache();
        for(const auto text:{u8"万帕诺亚格",u8"施泰亚莫阿克",u8"é中𠮷A",u8"A e\u0301 中",u8"§Y万帕诺亚格§!",u8"A£adm£中文"}) {
            const std::string_view source(text);
            const FormattedText layout(source);
            for(std::size_t limit=0;limit<=source.size();++limit) {
                const auto end=layout.prefix(limit);
                check(end<=limit,"prefix exceeded byte limit");
                check(valid_utf8(source.substr(0,end)),"prefix split UTF-8 encoding");
            }
            check(layout.prefix(source.size())==source.size(),"complete text was truncated");
        }
        const FormattedText wam(u8"万帕诺亚格");
        check(wam.prefix(8)==6,"long country name retained an incomplete character");
        const FormattedText combining(u8"e\u0301Z");
        check(combining.prefix(1)==0&&combining.prefix(2)==0&&combining.prefix(3)==3,
              "prefix split a combining sequence");
        const FormattedText colored_combining(u8"e§R\u0301§!Z");
        check(colored_combining.prefix(4)==0,"color directive split a combining sequence");
        const FormattedText icon(u8"A£adm£Z");
        for(std::size_t limit=2;limit<8;++limit)
            check(icon.prefix(limit)==1,"prefix split an icon token");
        check(icon.visible_text()==u8"A\ufffcZ","icon name entered visible text");
        const FormattedText flags("x@FRA@D01y");
        check(flags.visible_text()==u8"x\ufffc\ufffcy","country tags are inline objects");
        check(flags.prefix(3)==1&&flags.prefix(6)==5,"country tags cannot be truncated inside a tag");
        check(FormattedText("@FR @abc").visible_text()=="@FR @abc","incomplete tags remain literal text");
        check(FormattedText("@FRA",false).visible_text()=="@FRA","unformatted tags remain literal text");
        const FormattedText symbols(u8"A¤1{12Z");
        check(symbols.visible_text()==u8"A\ufffc\ufffcZ","currency and numbered symbols are atomic inline objects");
        check(symbols.prefix(3)==1&&symbols.prefix(5)==4,"symbol arguments cannot be truncated inside a native command");
        check(FormattedText("{1x").visible_text()=="{1x","incomplete numbered symbols remain literal text");
        const FormattedText color(u8"§Y中文§!");
        check(color.visible_text()==u8"中文","color code entered visible text");
        check(color.prefix(1)==0&&color.prefix(2)==0&&color.prefix(3)==3,"color directive split");
        const std::string punctuation=u8"中§Y文§!，中文";
        const FormattedText wrapped(punctuation);
        check(wrapped.line_before(3),"color directive blocked a Chinese line boundary");
        check(!wrapped.line_before(9)&&!wrapped.line_before(12),"line break separated punctuation from preceding text");
        check(wrapped.line_before(15),"Chinese line boundary after punctuation missing");
        check(native_wrap_before(wrapped,punctuation,8),"measurement missed the boundary before a colored Chinese glyph");
        check(!native_wrap_before(wrapped,punctuation,13),"measurement moved closing punctuation onto a new line");
        const std::string plain=u8"中文，中文";
        const FormattedText plain_boundaries(plain);
        for(std::size_t last_byte=3;last_byte<6;++last_byte)
            check(native_wrap_before(plain_boundaries,plain,last_byte),"measurement used a UTF-8 continuation byte as a line boundary");
        for(std::size_t last_byte=6;last_byte<9;++last_byte)
            check(!native_wrap_before(plain_boundaries,plain,last_byte),"measurement split closing punctuation from preceding Chinese text");
        check(!native_wrap_before(FormattedText("word word"),"word word",5),"measurement changed the native Latin word path");
        const FormattedText cluster(u8"中e§R\u0301§!文");
        check(!cluster.line_before(4)&&!cluster.line_before(7),"line break split a colored combining sequence");
        check(!native_wrap_before(cluster,u8"中e§R\u0301§!文",8),"measurement split a colored combining sequence");
        check(FormattedText(u8"§中文").visible_text()==u8"§中文","non-ASCII character consumed as a color code");
        check(FormattedText("\xa3" "yes ").visible_text()==u8"\ufffc","compiled icon with whitespace delimiter was lost");
        for(const auto text:{u8"é",u8"中",u8"𠮷"}) {
            const std::string_view source(text);
            for(std::size_t index=0;index<source.size();++index)
                check(native_scalar_start(source,index)==0,"queue position is not the character start");
            check(native_scalar_next(source,0)==source.size(),"iterator did not advance a complete scalar");
            for(std::size_t size=0;size<source.size();++size)
                check(native_measure_scalar(source.substr(0,size)).bytes==0,"range measured an incomplete character");
            check(native_measure_scalar(source).bytes==source.size(),"complete measured character was skipped");
        }
        const std::string native_color="\xa7Y"+std::string(u8"中文")+"\xa7!";
        check(FormattedText(native_color).visible_text()==u8"中文","compiled native color literal lost its meaning");
        check(layout_substring_caller(0x159ff80)&&layout_substring_caller(0x15a00a0),"list substring branch missing");
        check(!layout_substring_caller(0x1704af0),"unrelated substring caller was included");
        std::cout<<"PASS: native formatted text, bounded layout cache, complete prefixes and scalar positions\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
