#include "name_policy.hpp"
#include <cstdlib>
#include <iostream>

using namespace eu4unicode;
int main() {
    struct Example {const char *culture,*given,*family,*expected;};
    for(const auto& example:{
        Example{"chihan","祁镇","朱","朱祁镇"},
        Example{"japanese","Ieyasu","Tokugawa","Tokugawa Ieyasu"},
        Example{"togoku","家康","徳川","徳川家康"},
        Example{"korean","영수","김","김영수"},
        Example{"hungarian","János","Hunyadi","Hunyadi János"}}) {
        NamePolicy policy;
        if(!culture_name_policy(example.culture,policy)||
           format_person_name(example.given,example.family,policy)!=example.expected) return 1;
    }
    NamePolicy policy;
    if(culture_name_policy("tibetan_new",policy)||culture_name_policy("french",policy)) return 1;
    if(!parse_name_policy("surname_first middle_dot",policy)||
       format_person_name("亚诺什","匈雅提",policy)!="匈雅提·亚诺什") return 1;
    if(parse_name_policy("given_first space extra",policy)||policy.order!=NameOrder::surname_first) return 1;
    if(!parse_name_policy("given_first none",policy)||format_person_name("名","姓",policy)!="名姓") return 1;
    std::size_t offset=0;
    if(!surname_first(" ¿危",offset)||offset!=3||surname_first("x¿危",offset)) return 1;
    std::cout<<"Shared name policy tests passed\n";
}
