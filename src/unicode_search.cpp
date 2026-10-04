#include "unicode_search.hpp"
#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include "unicode_pinyin.hpp"
#include "unicode_search_fuzzy.hpp"
#include <algorithm>
#include <atomic>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace eu4unicode {
namespace {
struct Keys {
    std::string unicode,latin,simplified;
    std::vector<SearchSyllable> syllables;
    std::vector<std::vector<std::vector<std::string>>> fuzzy_readings;
};
std::atomic<unsigned> options{1};
struct CachedName {
    std::shared_ptr<const Keys> keys;
    std::list<std::string>::iterator position;
    std::size_t bytes;
};
thread_local std::unordered_map<std::string,CachedName> cache;
thread_local std::list<std::string> recent;
thread_local std::size_t cache_bytes=0;
thread_local std::uint64_t cache_generation=0;
thread_local unsigned cache_options=1;
std::uint32_t latin_equivalent(std::uint32_t scalar) {
    if((scalar>=0xc0&&scalar<=0xc6)||(scalar>=0xe0&&scalar<=0xe6)) return 'a';
    if(scalar==0xc7||scalar==0xe7) return 'c';
    if((scalar>=0xc8&&scalar<=0xcb)||(scalar>=0xe8&&scalar<=0xeb)) return 'e';
    if((scalar>=0xcc&&scalar<=0xcf)||(scalar>=0xec&&scalar<=0xef)) return 'i';
    if(scalar==0xd0||scalar==0xf0) return 'd';
    if(scalar==0xd1||scalar==0xf1) return 'n';
    if((scalar>=0xd2&&scalar<=0xd6)||(scalar>=0xf2&&scalar<=0xf6)||scalar==0xd8||scalar==0xf8) return 'o';
    if((scalar>=0xd9&&scalar<=0xdc)||(scalar>=0xf9&&scalar<=0xfc)) return 'u';
    if(scalar==0xdd||scalar==0xfd||scalar==0xff||scalar==0x178) return 'y';
    if(scalar==0xde||scalar==0xfe) return 't';
    if(scalar==0xdf||scalar==0x1e9e) return 's';
    return scalar;
}
std::string display_text(std::string_view text) {
    if(!valid_utf8(text)) throw std::invalid_argument("Invalid UTF-8 display name");
    std::string result;
    while(!text.empty()) {
        if(text.substr(0,2)=="\xc2\xa7"&&text.size()>2&&
           std::string_view("!RGBYWHOLCMVJKTSP").find(text[2])!=std::string_view::npos) {
            text.remove_prefix(3); continue;
        }
        const auto scalar=decode(text);
        result.append(text.data(),scalar.bytes);text.remove_prefix(scalar.bytes);
    }
    return result;
}
std::shared_ptr<const Keys> keys(std::string_view text) {
    const auto generation=pinyin_dictionary_generation();
    const auto configured=options.load(std::memory_order_acquire);
    if(generation!=cache_generation||configured!=cache_options) {
        cache.clear();recent.clear();cache_bytes=0;cache_generation=generation;cache_options=configured;
    }
    const auto owned=std::string(text);
    const auto found=cache.find(owned);
    if(found!=cache.end()) {
        recent.splice(recent.begin(),recent,found->second.position);
        return found->second.keys;
    }
    const auto display=display_text(text);
    auto result=std::make_shared<Keys>();
    result->unicode=search_key(display);
    result->latin=latin_search_key(display);
    result->simplified=simplified_search_text(display);
    result->syllables=search_syllables(result->simplified);
    if(configured&2) {
        result->fuzzy_readings.resize(result->syllables.size());
        for(std::size_t i=0;i<result->syllables.size();++i)
            for(const auto& reading:result->syllables[i].readings)
                result->fuzzy_readings[i].push_back(pinyin_fuzzy_forms(reading));
    }
    auto bytes=sizeof(CachedName)+sizeof(Keys)+owned.size()*2+result->unicode.size()+
        result->latin.size()+result->simplified.size()+result->syllables.capacity()*sizeof(SearchSyllable);
    for(const auto& syllable:result->syllables) {
        bytes+=syllable.literal.size()+syllable.readings.capacity()*sizeof(std::string);
        for(const auto& reading:syllable.readings) bytes+=reading.size();
    }
    bytes+=result->fuzzy_readings.capacity()*sizeof(decltype(result->fuzzy_readings)::value_type);
    for(const auto& readings:result->fuzzy_readings) {
        bytes+=readings.capacity()*sizeof(std::vector<std::string>);
        for(const auto& forms:readings) {
            bytes+=forms.capacity()*sizeof(std::string);
            for(const auto& form:forms) bytes+=form.size();
        }
    }
    constexpr std::size_t budget=32*1024*1024;
    if(bytes<=budget) {
        while(!recent.empty()&&(cache.size()>=32768||cache_bytes>budget-bytes)) {
            const auto old=cache.find(recent.back());cache_bytes-=old->second.bytes;
            cache.erase(old);recent.pop_back();
        }
        recent.push_front(owned);
        try { cache.emplace(owned,CachedName{result,recent.begin(),bytes}); }
        catch(...) { recent.pop_front(); throw; }
        cache_bytes+=bytes;
    }
    return result;
}
struct Pattern {
    std::string source,unicode,latin,simplified,phonetic;
    bool initials=false;
    explicit Pattern(std::string_view text):source(text),unicode(search_key(text)),latin(latin_search_key(text)),
        simplified(simplified_search_text(text)) {
        std::size_t letters=0;
        for(const auto c:pinyin_letters(simplified)) {
            if(c==' '||c=='\t'||c=='-'||c=='\'') continue;
            phonetic.push_back(c);
            if(c>='a'&&c<='z') ++letters;
        }
        initials=letters>=2;
        if(letters<2&&phonetic.find_first_not_of("abcdefghijklmnopqrstuvwxyz")==std::string::npos) phonetic.clear();
    }
};
const Pattern& pattern(std::string_view query) {
    thread_local std::unique_ptr<Pattern> current;
    if(!current||current->source!=query) current=std::make_unique<Pattern>(query);
    return *current;
}
bool prefix(std::string_view text,std::string_view value) { return text.substr(0,value.size())==value; }
bool phonetic_contains(const Keys& candidate,const Pattern& query,bool fuzzy=false) {
    const auto& units=candidate.syllables;
    if(query.phonetic.empty()) return false;
    const auto size=query.phonetic.size();
    std::vector<unsigned char> previous(size+1);
    for(std::size_t word=0;word<units.size();) {
        const auto end=units[word].word_end;
        const auto alternatives=units[word].readings.size()+1;
        std::vector<unsigned char> positions((size+1)*alternatives),next(positions.size());
        for(std::size_t pos=0;pos<size;++pos) positions[pos*alternatives]=previous[pos];
        for(std::size_t index=word;index<end;++index) {
            const auto& unit=units[index];
            positions[0]=1;
            std::fill(next.begin(),next.end(),static_cast<unsigned char>(0));
            for(std::size_t pos=0;pos<size;++pos) for(std::size_t selected=0;selected<alternatives;++selected) {
                if(!positions[pos*alternatives+selected]) continue;
                const auto remaining=std::string_view(query.phonetic).substr(pos);
                auto accept=[&](std::string_view value,bool partial,std::size_t branch) {
                    if(prefix(remaining,value)) next[(pos+value.size())*alternatives+branch]=1;
                    else if(partial&&prefix(value,remaining)) next[size*alternatives+branch]=1;
                };
                if(unit.literal==" "||unit.literal=="\t"||unit.literal=="-"||unit.literal=="'") next[pos*alternatives+selected]=1;
                else accept(unit.literal,false,selected);
                for(std::size_t branch=1;branch<alternatives;++branch) {
                    if(selected&&selected!=branch) continue;
                    const auto& reading=unit.readings[branch-1];
                    if(fuzzy) {
                        for(const auto& form:candidate.fuzzy_readings[index][branch-1]) {
                            accept(form,true,branch);
                            if(query.initials) accept(std::string_view(form).substr(0,1),false,branch);
                        }
                        continue;
                    }
                    accept(reading,true,branch);
                    if(query.initials) accept(std::string_view(reading).substr(0,1),false,branch);
                    if(reading.find('v')!=std::string::npos) {
                        auto common=reading;std::replace(common.begin(),common.end(),'v','u');accept(common,true,branch);
                    }
                }
            }
            if(std::any_of(next.begin()+size*alternatives,next.end(),[](unsigned char value){return value!=0;})) return true;
            positions.swap(next);
        }
        for(std::size_t pos=0;pos<size;++pos)
            previous[pos]=std::any_of(positions.begin()+pos*alternatives,positions.begin()+(pos+1)*alternatives,
                [](unsigned char value){return value!=0;});
        word=end;
    }
    return false;
}
bool tolerant_contains(const Keys& candidate,const Pattern& query) {
    const auto configured=search_options();
    return (configured.fuzzy_pinyin&&!candidate.fuzzy_readings.empty()&&phonetic_contains(candidate,query,true))||
        (configured.typo_tolerance&&pinyin_typo_matches(candidate.syllables,query.phonetic));
}
}
void set_search_options(SearchOptions value) noexcept {
    options.store((value.typo_tolerance?1u:0u)|(value.fuzzy_pinyin?2u:0u),std::memory_order_release);
}
SearchOptions search_options() noexcept {
    const auto configured=options.load(std::memory_order_acquire);
    return {(configured&1)!=0,(configured&2)!=0};
}
std::string latin_search_key(std::string_view text) {
    const auto canonical=canonical_text(text);
    auto remaining=std::string_view(canonical);
    std::string latin;
    while(!remaining.empty()) {
        const auto scalar=decode(remaining);
        latin+=encode(latin_equivalent(scalar.value));remaining.remove_prefix(scalar.bytes);
    }
    return search_key(latin);
}
bool display_search_contains(std::string_view name,std::string_view query) {
    const auto& match=pattern(query);
    const auto candidate=keys(name);
    return candidate->unicode.find(match.unicode)!=std::string::npos||
        candidate->latin.find(match.latin)!=std::string::npos||
        candidate->simplified.find(match.simplified)!=std::string::npos||
        phonetic_contains(*candidate,match)||tolerant_contains(*candidate,match);
}
bool country_search_contains(std::string_view name,std::string_view query) { return display_search_contains(name,query); }
std::pair<std::string,std::string> display_search_latin_keys(std::string_view name,std::string_view query) {
    const auto& match=pattern(query);
    return {keys(name)->latin,match.latin};
}
int display_search_distance(std::string_view name,std::string_view query) {
    const auto& match=pattern(query);
    const auto candidate=keys(name);
    if(candidate->unicode==match.unicode||candidate->latin==match.latin||candidate->simplified==match.simplified) return 0;
    if(candidate->unicode.find(match.unicode)!=std::string::npos||candidate->latin.find(match.latin)!=std::string::npos||
       candidate->simplified.find(match.simplified)!=std::string::npos||phonetic_contains(*candidate,match)) return 1;
    if(tolerant_contains(*candidate,match)) return 2;
    return -1;
}
}
