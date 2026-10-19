#include "yaml_input.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
}
int main() {
    using eu4unicode::yaml_parse_buffer;
    const std::string text=u8"l_english:\r\n test:0 \"中文𠮷\ufeff\"\r\n";
    const auto original=text;
    const auto normalized=yaml_parse_buffer(text);
    check(normalized&&*normalized=="\xef\xbb\xbf"+text,"missing signature supplied without changing content");
    check(text==original,"source buffer remains unchanged");
    check(!yaml_parse_buffer(*normalized),"existing signature is not duplicated");
    check(!yaml_parse_buffer({}),"empty input remains native");
    for(const auto invalid:{std::string("\xef"),std::string("\xef\xbb"),std::string("\xc0\xaf"),
                            std::string("\xed\xa0\x80"),std::string("\xf4\x90\x80\x80")})
        check(!yaml_parse_buffer(invalid),"malformed UTF-8 is not normalized");
    check(yaml_parse_buffer("l_english:\n key:0 \"ASCII\"\n").has_value(),"ASCII is valid UTF-8");
    check(yaml_parse_buffer("l_english:\n key:0 \"\x10-N\"\n").has_value(),"legacy ASCII payload remains available to import hook");
    std::cout<<"Native YAML input normalization passed\n";
}
