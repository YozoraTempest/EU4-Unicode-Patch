#include "native_search.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {
void check(bool value,const char* message) {
    if(!value) { std::cerr<<"FAIL: "<<message<<'\n';std::exit(1); }
}
std::uint64_t native_find(const char*,std::uint64_t,std::uint64_t,const char*,std::uint64_t) { return 7; }
std::string seen_name,seen_query;
std::int64_t native_distance(const eu4unicode::EngineString* name,const eu4unicode::EngineString* query) {
    seen_name.assign(name->data(),static_cast<std::size_t>(name->size));
    seen_query.assign(query->data(),static_cast<std::size_t>(query->size));
    return 19;
}
eu4unicode::EngineString view(const std::string& text) {
    eu4unicode::EngineString value{};value.size=text.size();
    if(text.size()<16) {value.capacity=15;std::memcpy(value.storage.inline_bytes,text.c_str(),text.size()+1);}
    else {value.capacity=text.size();value.storage.pointer=text.c_str();}
    return value;
}
}
int main() {
    using namespace eu4unicode;
    const std::string name=u8"长安",query="ca";
    for(const auto caller:{0xefc394,0x1141feb,0x11420e2}) {
        check(find_display_name(caller,name.data(),name.size(),0,query.data(),query.size(),native_find)==0,"scoped name matching");
        check(find_display_name(caller,name.data(),name.size(),name.size()+1,query.data(),query.size(),native_find)==UINT64_MAX,"invalid search start");
        check(find_display_name(caller,name.data(),name.size(),0,"zz",2,native_find)==UINT64_MAX,"unmatched name");
    }
    check(find_display_name(0x17041cf,name.data(),name.size(),0,query.data(),query.size(),native_find)==7,"unrelated search preserves byte offsets");
    const auto native_name=view(name),native_query=view(query);
    for(const auto caller:{0x1142192,0x11421b2})
        check(province_search_distance(caller,&native_name,&native_query,native_distance)==1,"both province ranking callers accept pinyin");
    const std::string latin=u8"École Straße Long Province",other="OTHER";
    const auto native_latin=view(latin),native_other=view(other);
    check(province_search_distance(0x1142192,&native_latin,&native_other,native_distance)==19&&
        seen_name=="ecole strase long province"&&seen_query=="other","native fuzzy distance receives scalar-normalized borrowed copies");
    check(std::string(native_latin.data(),native_latin.size)==latin&&std::string(native_other.data(),native_other.size)==other,"engine display and query remain unchanged");
    check(province_search_distance(0x11421b3,&native_latin,&native_other,native_distance)==19&&
        seen_name==latin&&seen_query==other,"other distance callers remain native");
    std::cout<<"Native country/province search ABI checks passed.\n";
}
