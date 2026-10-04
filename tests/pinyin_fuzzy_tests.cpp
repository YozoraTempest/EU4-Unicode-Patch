#include "unicode_search_fuzzy.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <random>

namespace {
// Independent full-matrix optimal-string-alignment reference. The production
// matcher consumes a pronunciation graph instead of computing this matrix.
unsigned distance(std::string_view a,std::string_view b) {
    std::vector<std::vector<unsigned>> table(a.size()+1,std::vector<unsigned>(b.size()+1));
    for(std::size_t i=0;i<=a.size();++i) table[i][0]=static_cast<unsigned>(i);
    for(std::size_t j=0;j<=b.size();++j) table[0][j]=static_cast<unsigned>(j);
    for(std::size_t i=1;i<=a.size();++i) for(std::size_t j=1;j<=b.size();++j) {
        table[i][j]=(std::min)({table[i-1][j]+1,table[i][j-1]+1,table[i-1][j-1]+(a[i-1]!=b[j-1])});
        if(i>1&&j>1&&a[i-1]==b[j-2]&&a[i-2]==b[j-1])
            table[i][j]=(std::min)(table[i][j],table[i-2][j-2]+1);
    }
    return table.back().back();
}
void verify(const std::vector<eu4unicode::SearchSyllable>& units,const std::vector<std::string>& readings,
            const std::string& query) {
    const auto expected=query.size()>=5&&std::any_of(readings.begin(),readings.end(),[&](const std::string& reading){
        return distance(reading,query)<=1;
    });
    if(eu4unicode::pinyin_typo_matches(units,query)!=expected) {
        std::cerr<<"FAIL: graph match differs from reference for "<<query<<'\n';std::exit(1);
    }
}
}
int main() {
    using eu4unicode::SearchSyllable;
    const std::vector<SearchSyllable> units{{u8"重",{"chong","zhong"},2},{u8"庆",{"qing","jing"},2},
        {u8"长",{"chang","zhang"},4},{u8"安",{"an","an"},4}};
    const std::vector<std::string> readings{"chongqingchangan","chongqingzhangan","zhongjingchangan","zhongjingzhangan"};
    std::size_t cases=0;
    for(const auto& reading:readings) {
        verify(units,readings,reading);++cases;
        for(std::size_t i=0;i<reading.size();++i) {
            auto query=reading;query.erase(i,1);verify(units,readings,query);++cases;
            if(i+1<reading.size()) {query=reading;std::swap(query[i],query[i+1]);verify(units,readings,query);++cases;}
            for(char value='a';value<='z';++value) {
                query=reading;query[i]=value;verify(units,readings,query);++cases;
            }
        }
        for(std::size_t i=0;i<=reading.size();++i) for(char value='a';value<='z';++value) {
            auto query=reading;query.insert(i,1,value);verify(units,readings,query);++cases;
        }
    }
    std::mt19937 random(0x4e00);
    for(unsigned i=0;i<4000;++i) {
        auto query=readings[random()%readings.size()];
        for(unsigned edits=1+random()%4;edits;--edits) {
            const auto offset=random()%query.size();
            switch(random()%4) {
            case 0:query[offset]=static_cast<char>('a'+random()%26);break;
            case 1:query.insert(offset,1,static_cast<char>('a'+random()%26));break;
            case 2:query.erase(offset,1);break;
            case 3:if(offset+1<query.size()) std::swap(query[offset],query[offset+1]);break;
            }
        }
        verify(units,readings,query);++cases;
    }
    // Repeated two-reading words must not form an exponential string list.
    std::vector<SearchSyllable> large;
    for(unsigned i=0;i<40;++i) {
        const auto end=large.size()+2;
        large.push_back({u8"重",{"chong","zhong"},end});large.push_back({u8"庆",{"qing","jing"},end});
    }
    std::string full;
    for(unsigned i=0;i<40;++i) full+="chongqing";
    full[full.size()/2]='x';
    if(!eu4unicode::pinyin_typo_matches(large,full)) {std::cerr<<"FAIL: large pronunciation graph\n";return 1;}
    std::cout<<cases<<" pronunciation-graph/reference checks and a 40-word graph passed.\n";
}
