#include "unicode_search_fuzzy.hpp"
#include <algorithm>

namespace eu4unicode {
namespace {
// A distance-one automaton runs over the pronunciation graph. Unlike a single
// string distance, this avoids enumerating every combination of word readings.
struct State {
    std::vector<unsigned char> exact,edited,swap;
    explicit State(std::size_t size):exact(size+1),edited(size+1),swap(size+1) {}
    void merge(const State& other) {
        for(std::size_t i=0;i<exact.size();++i) {
            exact[i]|=other.exact[i];edited[i]|=other.edited[i];swap[i]|=other.swap[i];
        }
    }
    void consume(char value,std::string_view query) {
        State next(query.size());
        for(std::size_t i=0;i<=query.size();++i) {
            if(exact[i]) {
                next.edited[i]=1; // Extra character in the name's pronunciation.
                if(i<query.size()) {
                    if(value==query[i]) next.exact[i+1]=1;
                    else next.edited[i+1]=1;
                    if(i+1<query.size()&&value==query[i+1]) {
                        next.edited[i+2]=1; // Extra character typed in the query.
                        next.swap[i]=1;
                    }
                }
            }
            if(i<query.size()&&edited[i]&&value==query[i]) next.edited[i+1]=1;
            if(i+1<query.size()&&swap[i]&&value==query[i]) next.edited[i+2]=1;
        }
        *this=std::move(next);
    }
    bool alive() const {
        const auto any=[](unsigned char value){return value!=0;};
        return std::any_of(exact.begin(),exact.end(),any)||std::any_of(edited.begin(),edited.end(),any)||
            std::any_of(swap.begin(),swap.end(),any);
    }
};
void consume_syllable(State& state,std::string_view text,std::string_view query) {
    for(const auto value:text) {
        if(value=='v') {
            auto common=state;common.consume('u',query);state.consume(value,query);state.merge(common);
        } else state.consume(value,query);
    }
}
}
bool pinyin_typo_matches(const std::vector<SearchSyllable>& units,std::string_view query) {
    if(query.size()<5||query.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string_view::npos) return false;
    // Short initials and ordinary Latin names keep the game's existing behavior.
    if(std::count_if(units.begin(),units.end(),[](const SearchSyllable& unit){return !unit.readings.empty();})<2) return false;
    State current(query.size());current.exact[0]=1;
    for(std::size_t word=0;word<units.size();) {
        const auto end=units[word].word_end;
        State merged(query.size());
        const auto alternatives=(std::max)(std::size_t{1},units[word].readings.size());
        for(std::size_t branch=0;branch<alternatives;++branch) {
            auto state=current;
            for(std::size_t i=word;i<end;++i) {
                const auto& unit=units[i];
                if(!unit.readings.empty()) consume_syllable(state,unit.readings[branch],query);
                else if(unit.literal!=" "&&unit.literal!="\t"&&unit.literal!="-"&&unit.literal!="'") {
                    if(unit.literal.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos) return false;
                    consume_syllable(state,unit.literal,query);
                }
            }
            merged.merge(state);
        }
        current=std::move(merged);
        if(!current.alive()) return false;
        word=end;
    }
    return current.exact.back()||current.edited.back()||current.exact[query.size()-1];
}
std::vector<std::string> pinyin_fuzzy_forms(std::string_view syllable) {
    std::vector<std::string> forms{std::string(syllable)};
    const auto add=[&](std::string value) {
        if(std::find(forms.begin(),forms.end(),value)==forms.end()) forms.push_back(std::move(value));
    };
    if(syllable.size()>1&&(syllable[0]=='z'||syllable[0]=='c'||syllable[0]=='s')) {
        auto value=std::string(syllable);
        if(value[1]=='h') value.erase(1,1);else value.insert(1,1,'h');
        add(std::move(value));
    } else if(!syllable.empty()&&(syllable[0]=='n'||syllable[0]=='l')) {
        auto value=std::string(syllable);value[0]=value[0]=='n'?'l':'n';add(std::move(value));
    }
    const auto count=forms.size();
    for(std::size_t i=0;i<count;++i) {
        auto value=forms[i];
        if(value.size()>=3&&(value.substr(value.size()-3)=="eng"||value.substr(value.size()-3)=="ing")) {
            value.pop_back();add(std::move(value));
        } else if(value.size()>=2&&(value.substr(value.size()-2)=="en"||value.substr(value.size()-2)=="in")) {
            value.push_back('g');add(std::move(value));
        }
    }
    const auto aliases=forms.size();
    for(std::size_t i=0;i<aliases;++i) if(forms[i].find('v')!=std::string::npos) {
        auto value=forms[i];std::replace(value.begin(),value.end(),'v','u');add(std::move(value));
    }
    return forms;
}
}
