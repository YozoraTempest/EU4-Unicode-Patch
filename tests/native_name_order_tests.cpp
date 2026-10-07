#include "native_name_order.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace eu4unicode;
namespace {
std::string result;
int appends=0,assignments=0;
void* append(EngineString* target,const char* data,std::uint64_t size) {
    ++appends;result.assign(target->data(),static_cast<std::size_t>(target->size));
    result.append(data,static_cast<std::size_t>(size));return target;
}
void* assign(EngineString* target,const char* data,std::uint64_t size) {
    ++assignments;result.assign(data,static_cast<std::size_t>(size));return target;
}
EngineString borrowed(const std::string& text) {
    EngineString value{};value.storage.pointer=text.data();value.size=text.size();
    value.capacity=std::max<std::size_t>(16,text.size());return value;
}
void check(const std::string& first,const std::string& last,const std::string& expected,bool reordered) {
    auto given=borrowed(first),suffix=borrowed(last);
    appends=assignments=0;
    if(append_person_name(&given,&suffix)!=&given||result!=expected||
       assignments!=(reordered?1:0)||appends!=(reordered?0:1)) {
        std::cerr<<"Name order test failed\n";std::exit(1);
    }
}
}
int main() {
    native_name_append=append;native_name_assign=assign;
    check("德操"," ¿危","危德操",true);
    check("Reimu"," ¿Hakurei","HakureiReimu",true);
    check("景哲",std::string(" ")+char(0xbf)+"扁","扁景哲",true);
    check("Jean"," de Valois","Jean de Valois",false);
    check("中文"," 姓氏","中文 姓氏",false);
    check("A"," x¿B","A x¿B",false);
    check("A","¿B","A¿B",false);
    check(""," ¿危","危",true);
    check("A"," ¿","A",true);
    check("A","","A",false);
    check("名名名名名名名名名名"," ¿姓姓姓姓姓姓姓姓","姓姓姓姓姓姓姓姓名名名名名名名名名名",true);
    std::cout<<"Name order tests passed\n";
}
