#include "search_resources.hpp"
#include "unicode_pinyin.hpp"
#include <charconv>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace eu4unicode {
namespace {
std::string_view trim(std::string_view value) {
    constexpr auto spaces=" \t\r\n";
    const auto first=value.find_first_not_of(spaces);
    if(first==value.npos) return {};
    return value.substr(first,value.find_last_not_of(spaces)-first+1);
}
std::string lower(std::string_view value) {
    std::string result(value);
    for(auto& c:result) if(c>='A'&&c<='Z') c=static_cast<char>(c+'a'-'A');
    return result;
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input) throw std::runtime_error("Cannot read search resource: "+path.filename().string());
    std::string result((std::istreambuf_iterator<char>(input)),{});
    if(input.bad()) throw std::runtime_error("Cannot read search resource: "+path.filename().string());
    if(result.compare(0,3,"\xef\xbb\xbf")==0) result.erase(0,3);
    return result;
}
}
SearchOptions parse_search_config(std::string_view content) {
    SearchOptions result{true,false};
    if(content.substr(0,3)=="\xef\xbb\xbf") content.remove_prefix(3);
    bool search=false;
    while(!content.empty()) {
        const auto end=content.find('\n');
        const auto row=trim(content.substr(0,end));
        content=end==content.npos?std::string_view{}:content.substr(end+1);
        if(row.empty()||row.front()==';'||row.front()=='#') continue;
        if(row.front()=='['&&row.back()==']') {search=lower(trim(row.substr(1,row.size()-2)))=="search";continue;}
        if(!search) continue;
        const auto equal=row.find('=');
        if(equal==row.npos) continue;
        const auto key=lower(trim(row.substr(0,equal)));
        auto value=trim(row.substr(equal+1));
        if(value.size()>=2&&value.front()=='"'&&value.back()=='"') value=value.substr(1,value.size()-2);
        if(value.empty()) continue;
        int number=0;
        const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
        if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()) continue;
        if(key=="typo_tolerance") result.typo_tolerance=number!=0;
        else if(key=="fuzzy_pinyin") result.fuzzy_pinyin=number!=0;
    }
    return result;
}
bool load_search_resources(const std::filesystem::path& directory) {
    const auto config=directory/"config.ini";
    set_search_options(std::filesystem::exists(config)?parse_search_config(read(config)):SearchOptions{true,false});
    const auto dictionary=directory/"pinyin.txt";
    if(!std::filesystem::exists(dictionary)) return false;
    set_pinyin_dictionary(read(dictionary));
    return true;
}
}
