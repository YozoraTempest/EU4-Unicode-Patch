#include "localized_format.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition) { if(!condition) throw std::runtime_error("Localized date/title formatting check failed"); }
}
int main() {
    using namespace eu4unicode;
    check(chinese_date("8","十月","1356")=="1356年十月8日");
    check(chinese_date("31","12月","1444")=="1444年12月31日");
    check(chinese_month_year("十一月, ","1444")=="1444年十一月");
    check(chinese_month_year("十月 ","1356")=="1356年十月");
    check(chinese_date("8","October","1356").empty());
    check(chinese_date("VIII","十月","1356").empty());
    check(chinese_month_year("一月","year").empty());
    check(chinese_battle_title("之战"," 巴黎")=="巴黎之战");
    check(chinese_battle_title("之围"," London")=="London之围");
    check(chinese_battle_title("之戰"," 倫敦")=="倫敦之戰");
    check(chinese_battle_title("Battle of"," Paris").empty());
    check(chinese_battle_title("占领"," 北京").empty());
    check(chinese_battle_title("之战","").empty());
    std::cout<<"Localized dates and battle titles passed.\n";
}
