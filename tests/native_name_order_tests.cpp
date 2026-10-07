#include "native_name_order.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unordered_map>

using namespace eu4unicode;
namespace {
std::string result;
int appends=0,assignments=0;
std::unordered_map<std::string,std::string> policies;
const char* lookup(const char* key) {
    const auto found=policies.find(key);
    return found==policies.end()?nullptr:found->second.c_str();
}
void* append(EngineString* target,const char* data,std::uint64_t size) {
    ++appends;result.assign(target->data(),static_cast<std::size_t>(target->size));
    result.append(data,static_cast<std::size_t>(size));return target;
}
void* assign(EngineString* target,const char* data,std::uint64_t size) {
    ++assignments;result.assign(data,static_cast<std::size_t>(size));return target;
}
EngineString borrowed(const std::string& text) {
    EngineString value{};value.size=text.size();
    if(text.size()<16) {
        value.capacity=15;std::memcpy(value.storage.inline_bytes,text.c_str(),text.size()+1);
    } else {
        value.storage.pointer=text.data();value.capacity=text.size();
    }
    return value;
}
void check(const std::string& first,const std::string& last,const std::string& expected,bool reordered,
           const std::string& culture={}) {
    auto given=borrowed(first),suffix=borrowed(last);
    NativeNameCulture native_culture{};native_culture.key=borrowed(culture);
    NativeNamePerson person{};person.culture=&native_culture;
    appends=assignments=0;
    if(append_person_name(&given,&suffix,&person)!=&given||result!=expected||
       std::string_view(suffix.data(),static_cast<std::size_t>(suffix.size))!=last||
       std::string_view(native_culture.key.data(),static_cast<std::size_t>(native_culture.key.size))!=culture||
       assignments!=(reordered?1:0)||appends!=(reordered?0:1)) {
        std::cerr<<"Name order test failed: "<<culture<<" / "<<first<<" / "<<last<<" -> "<<result<<'\n';std::exit(1);
    }
}
}
int main() {
    native_name_append=append;native_name_assign=assign;
    native_name_policy_lookup=lookup;
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
    check("János"," Hunyadi","Hunyadi János",true,"hungarian");
    check("János"," ¿Hunyadi","Hunyadi János",true,"hungarian");
    check("亚诺什"," 匈雅提","匈雅提 亚诺什",true,"hungarian");
    check("祁镇"," 朱","朱祁镇",true,"chihan");
    check("𠮷"," 王","王𠮷",true,"cantonese");
    check("家康"," 徳川","徳川家康",true,"togoku");
    check("영수"," 김","김영수",true,"korean");
    check("Ieyasu"," Tokugawa","Tokugawa Ieyasu",true,"japanese");
    check("Hongwi"," Yi","Yi Hongwi",true,"korean");
    check("Jean Philippe"," de Valois","Jean Philippe de Valois",false,"french");
    check("Jean Philippe"," de Valois","de Valois Jean Philippe",true,"hungarian");
    check("中文"," 姓氏","中文 姓氏",false,"french");
    check("བོད"," མི","བོད མི",false,"tibetan_new");
    check("完整姓名","","完整姓名",false,"chihan");
    check("János","","János",false,"hungarian");
    check(""," Hunyadi","Hunyadi",true,"hungarian");
    check("Reimu"," ¿Hakurei·","Hakurei·Reimu",true);
    check("János"," ¿Hunyadi ","Hunyadi János",true,"hungarian");
    policies["EU4_UNICODE_NAME_hungarian"]="surname_first middle_dot";
    check("亚诺什"," 匈雅提","匈雅提·亚诺什",true,"hungarian");
    check("亚诺什"," ¿匈雅提·","匈雅提·亚诺什",true,"hungarian");
    policies["EU4_UNICODE_NAME_hungarian"]="given_first space";
    check("János"," Hunyadi","János Hunyadi",true,"hungarian");
    check("János"," ¿Hunyadi","Hunyadi János",true,"hungarian");
    policies["EU4_UNICODE_NAME_custom_culture"]="surname_first none";
    check("名字"," 姓氏","姓氏名字",true,"custom_culture");
    policies["EU4_UNICODE_NAME_custom_culture"]="given_first none";
    check("名字"," 姓氏","名字姓氏",true,"custom_culture");
    policies["EU4_UNICODE_NAME_hungarian"]="surname_first invalid";
    check("János"," Hunyadi","Hunyadi János",true,"hungarian");
    policies["EU4_UNICODE_NAME_custom_culture"]="surname_first space extra";
    check("名字"," 姓氏","名字 姓氏",false,"custom_culture");
    policies["EU4_UNICODE_NAME_custom_culture"]=std::string(64,'x');
    check("名字"," 姓氏","名字 姓氏",false,"custom_culture");
    NamePolicy policy;
    if(!parse_name_policy(" \tsurname_first auto\r\n",policy)||policy.order!=NameOrder::surname_first||
       policy.separator!=NameSeparator::automatic) return 1;
    for(const auto invalid:{"","surname_first","reverse space","given_first dot","given_first space extra"}) {
        if(parse_name_policy(invalid,policy)||policy.order!=NameOrder::surname_first||
           policy.separator!=NameSeparator::automatic) return 1;
    }
    // In particular, no person/culture pointer is needed for legacy-only names.
    const std::string first="德操",last=" ¿危";
    auto given=borrowed(first),suffix=borrowed(last);
    append_person_name(&given,&suffix,nullptr);
    if(result!="危德操") return 1;
    std::cout<<"Name order tests passed\n";
}
