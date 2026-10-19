#include "search_resources.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
}
int main() {
    using namespace eu4unicode;
    for(const auto bom:{std::string(),std::string("\xef\xbb\xbf")}) {
        const auto options=parse_search_config(bom+"; comment\r\n[other]\ntypo_tolerance=1\n[SEARCH]\n TYPO_TOLERANCE = 0\r\nFuzzy_Pinyin=1\n");
        check(!options.typo_tolerance&&options.fuzzy_pinyin,"INI section, case, whitespace and BOM");
    }
    check(parse_search_config("[search]\ntypo_tolerance=bad\nfuzzy_pinyin=\n").typo_tolerance,"invalid values keep defaults");
    const auto path=std::filesystem::temp_directory_path()/("eu4-search-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::filesystem::remove_all(path);}} cleanup{path};
    check(!load_search_resources(path),"missing resources use builtin defaults");
    check(search_options().typo_tolerance&&!search_options().fuzzy_pinyin,"default options");
    std::ofstream(path/"config.ini",std::ios::binary)<<"\xef\xbb\xbf[search]\ntypo_tolerance=0\nfuzzy_pinyin=1\n";
    std::ofstream(path/"pinyin.txt",std::ios::binary)<<u8"\ufeff𠮷野: ji ye\r\n";
    check(load_search_resources(path),"custom dictionary loaded");
    check(!search_options().typo_tolerance&&search_options().fuzzy_pinyin,"config applied to actual search core");
    check(country_search_contains(u8"𠮷野","jy"),"custom supplementary dictionary used by actual matcher");
    std::ofstream(path/"pinyin.txt",std::ios::binary)<<u8"𠮷野: ji\n";
    bool rejected=false;
    try {load_search_resources(path);} catch(const std::invalid_argument&) {rejected=true;}
    check(rejected&&country_search_contains(u8"𠮷野","jy"),"invalid dictionary cannot replace a valid dictionary");
    std::cout<<"Search configuration and dictionary loading passed\n";
}
