#include "legacy_text.hpp"
#include <iostream>
#include <string>

namespace {
int nibble(char c) {
    if(c>='0'&&c<='9') return c-'0';
    if(c>='a'&&c<='f') return c-'a'+10;
    return -1;
}
}
int main() {
    std::string line;
    while(std::getline(std::cin,line)) {
        if(line.size()<2||line[1]!=' '||(line[0]!='r'&&line[0]!='u')||(line.size()-2)%2) return 2;
        std::string input;
        for(std::size_t i=2;i<line.size();i+=2) {
            const auto high=nibble(line[i]),low=nibble(line[i+1]);
            if(high<0||low<0) return 2;
            input.push_back(static_cast<char>(high*16+low));
        }
        const auto result=eu4unicode::decode_legacy_text(input,line[0]=='r'?
            eu4unicode::LegacyPayload::raw_bytes:eu4unicode::LegacyPayload::cp1252_in_utf8);
        if(result.error!=eu4unicode::LegacyError::none) {
            std::cout<<"ERR "<<static_cast<int>(result.error)<<' '<<result.error_offset<<'\n';continue;
        }
        std::cout<<"OK "<<result.sequences<<' '<<result.relocated<<' ';
        constexpr char hex[]="0123456789abcdef";
        for(const auto c:result.text) {
            const auto byte=static_cast<unsigned char>(c);
            std::cout<<hex[byte>>4]<<hex[byte&15];
        }
        std::cout<<'\n';
    }
    return std::cin.bad()?1:0;
}
