#include "localized_format.hpp"
#include <algorithm>

namespace eu4unicode {
namespace {
std::string_view trim(std::string_view value) {
    while(!value.empty()&&(value.front()==' '||value.front()==',')) value.remove_prefix(1);
    while(!value.empty()&&(value.back()==' '||value.back()==',')) value.remove_suffix(1);
    return value;
}
bool number(std::string_view value) {
    return !value.empty()&&std::all_of(value.begin(),value.end(),[](char c){return c>='0'&&c<='9';});
}
}
std::string chinese_month_year(std::string_view month,std::string_view year) {
    month=trim(month);year=trim(year);
    if(!number(year)||month.size()<3||month.substr(month.size()-3)!="月") return {};
    return std::string(year)+"年"+std::string(month);
}
std::string chinese_date(std::string_view day,std::string_view month,std::string_view year) {
    day=trim(day);
    auto result=chinese_month_year(month,year);
    if(result.empty()||!number(day)) return {};
    return result+std::string(day)+"日";
}
std::string chinese_battle_title(std::string_view prefix,std::string_view place) {
    prefix=trim(prefix);place=trim(place);
    if(place.empty()||(prefix!="之战"&&prefix!="之围"&&prefix!="之戰"&&prefix!="之圍")) return {};
    return std::string(place)+std::string(prefix);
}
}
