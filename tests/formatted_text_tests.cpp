#include "formatted_text.hpp"
#include "unicode_text.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace eu4unicode;
void check(bool value,const char* message) {
    if(!value) throw std::runtime_error(message);
}
int main() {
    try {
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
        const FormattedText color(u8"§Y中文§!");
        check(color.visible_text()==u8"中文","color code entered visible text");
        check(color.prefix(1)==0&&color.prefix(2)==0&&color.prefix(3)==3,"color directive split");
        for(const auto text:{u8"é",u8"中",u8"𠮷"}) {
            const std::string_view source(text);
            for(std::size_t index=0;index<source.size();++index)
                check(native_scalar_start(source,index)==0,"queue position is not the character start");
            check(native_scalar_next(source,0)==source.size(),"iterator did not advance a complete scalar");
        }
        const std::string native_color="\xa7Y"+std::string(u8"中文")+"\xa7!";
        check(FormattedText(native_color).visible_text()==u8"中文","compiled native color literal lost its meaning");
        check(layout_substring_caller(0x159ff80)&&layout_substring_caller(0x15a00a0),"list substring branch missing");
        check(!layout_substring_caller(0x1704af0),"unrelated substring caller was included");
        std::cout<<"PASS: native formatted text, complete prefixes and scalar positions\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
