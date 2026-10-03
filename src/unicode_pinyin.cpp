#include "unicode_pinyin.hpp"
#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include <icu.h>
#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <stdexcept>

namespace eu4unicode {
namespace {
struct PhraseEntry { const char* phrase; const char* readings; };
#include "pinyin_data.inc"
using Readings=std::vector<std::vector<std::string>>;
struct Dictionary {
    std::map<std::string,Readings,std::less<>> phrases;
    std::size_t max_characters=0;
    std::uint64_t generation=0;
};
std::shared_ptr<const Dictionary> dictionary=std::make_shared<Dictionary>();
std::atomic<std::uint64_t> generation{0};
std::string_view trim(std::string_view text) {
    const auto first=text.find_first_not_of(" \t\r\n");
    return first==std::string_view::npos?std::string_view{}:text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
}
Readings parse_readings(std::string_view text) {
    Readings result;
    while(!text.empty()) {
        const auto end=text.find('|');
        auto reading=text.substr(0,end);
        std::vector<std::string> parts;
        while(!(reading=trim(reading)).empty()) {
            const auto space=reading.find_first_of(" \t");
            const auto part=reading.substr(0,space);
            auto value=part.find_first_not_of("abcdefghijklmnopqrstuvwxyz")==std::string_view::npos?
                std::string(part):pinyin_letters(part);
            if(value.empty()||value.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos)
                throw std::invalid_argument("Invalid pinyin syllable");
            parts.push_back(std::move(value));
            if(space==std::string_view::npos) break;
            reading.remove_prefix(space+1);
        }
        if(parts.empty()) throw std::invalid_argument("Empty pinyin reading");
        result.push_back(std::move(parts));
        if(end==std::string_view::npos) break;
        text.remove_prefix(end+1);
    }
    return result;
}
void add_reading(SearchSyllable& syllable,const std::string& reading) {
    if(std::find(syllable.readings.begin(),syllable.readings.end(),reading)==syllable.readings.end())
        syllable.readings.push_back(reading);
}
}
std::string pinyin_letters(std::string_view text) {
    const auto normalized=decomposed_text(search_key(text));
    std::string result;
    auto rest=std::string_view(normalized);
    while(!rest.empty()) {
        const auto scalar=decode(rest);
        if(scalar.value==0x308&&!result.empty()&&result.back()=='u') result.back()='v';
        else if(u_charType(static_cast<UChar32>(scalar.value))!=U_NON_SPACING_MARK) result+=encode(scalar.value);
        rest.remove_prefix(scalar.bytes);
    }
    return result;
}
std::string simplified_search_text(std::string_view text) {
    return search_key(transliterated_text(search_key(text),"Traditional-Simplified"));
}
std::uint64_t pinyin_dictionary_generation() noexcept { return std::atomic_load(&dictionary)->generation; }
void set_pinyin_dictionary(std::string_view content) {
    if(!valid_utf8(content)) throw std::invalid_argument("Invalid UTF-8 pinyin dictionary");
    if(content.substr(0,3)=="\xef\xbb\xbf") content.remove_prefix(3);
    auto next=std::make_shared<Dictionary>();
    while(!content.empty()) {
        const auto end=content.find('\n');
        const auto raw=content.substr(0,end);
        const auto line=trim(raw.substr(0,raw.find('#')));
        if(!line.empty()) {
            const auto colon=line.find(':');
            if(colon==std::string_view::npos) throw std::invalid_argument("Pinyin dictionary line requires a colon");
            const auto phrase=simplified_search_text(trim(line.substr(0,colon)));
            auto remaining=std::string_view(phrase);
            std::size_t characters=0;
            while(!remaining.empty()) {
                const auto scalar=decode(remaining);
                if(!u_hasBinaryProperty(static_cast<UChar32>(scalar.value),UCHAR_IDEOGRAPHIC))
                    throw std::invalid_argument("Pinyin phrase must contain only Han characters");
                remaining.remove_prefix(scalar.bytes); ++characters;
            }
            if(!characters) throw std::invalid_argument("Empty pinyin phrase");
            auto readings=parse_readings(line.substr(colon+1));
            for(const auto& reading:readings)
                if(reading.size()!=characters) throw std::invalid_argument("Pinyin syllable count differs from phrase length");
            auto& stored=next->phrases[phrase];
            for(auto& reading:readings) if(std::find(stored.begin(),stored.end(),reading)==stored.end()) stored.push_back(std::move(reading));
            next->max_characters=(std::max)(next->max_characters,characters);
        }
        if(end==std::string_view::npos) break;
        content.remove_prefix(end+1);
    }
    next->generation=generation.fetch_add(1,std::memory_order_acq_rel)+1;
    std::atomic_store(&dictionary,std::shared_ptr<const Dictionary>(next));
}
std::vector<SearchSyllable> search_syllables(std::string_view simplified) {
    if(!valid_utf8(simplified)) throw std::invalid_argument("Invalid UTF-8 name");
    const auto overrides=std::atomic_load(&dictionary);
    std::vector<SearchSyllable> result;
    std::vector<std::size_t> offsets;
    auto remaining=simplified;
    while(!remaining.empty()) {
        const auto scalar=decode(remaining);
        offsets.push_back(simplified.size()-remaining.size());
        result.push_back({std::string(remaining.substr(0,scalar.bytes)),{},result.size()+1});
        remaining.remove_prefix(scalar.bytes);
    }
    offsets.push_back(simplified.size());
    const auto maximum=(std::max)(builtin_max_characters,overrides->max_characters);
    for(std::size_t i=0;i<result.size();) {
        const auto scalar=static_cast<UChar32>(decode(result[i].literal).value);
        if(!u_hasBinaryProperty(scalar,UCHAR_IDEOGRAPHIC)) {
            if(scalar>=0x80&&u_getIntPropertyValue(scalar,UCHAR_SCRIPT)==USCRIPT_LATIN)
                result[i].literal=pinyin_letters(result[i].literal);
            ++i; continue;
        }
        Readings readings;
        std::size_t length=0;
        // User phrases have priority, including shorter explicit overrides.
        for(int pass=0;pass<2&&!length;++pass) {
            const auto limit=(std::min)(maximum,result.size()-i);
            for(std::size_t count=limit;count>0;--count) {
                const auto phrase=simplified.substr(offsets[i],offsets[i+count]-offsets[i]);
                if(pass==0) {
                    const auto found=overrides->phrases.find(phrase);
                    if(found!=overrides->phrases.end()) { readings=found->second; length=count; break; }
                } else {
                    const auto found=std::lower_bound(std::begin(builtin_phrases),std::end(builtin_phrases),phrase,
                        [](const PhraseEntry& entry,std::string_view key){return std::string_view(entry.phrase)<key;});
                    if(found!=std::end(builtin_phrases)&&found->phrase==phrase) { readings=parse_readings(found->readings); length=count; break; }
                }
            }
        }
        if(!length) {
            const auto reading=pinyin_letters(transliterated_text(result[i].literal,"Han-Latin"));
            if(!reading.empty()&&reading.find_first_not_of("abcdefghijklmnopqrstuvwxyz")==std::string::npos)
                add_reading(result[i],reading);
            ++i; continue;
        }
        for(std::size_t j=0;j<length;++j) {
            result[i+j].word_end=i+length;
            for(const auto& reading:readings) result[i+j].readings.push_back(reading[j]);
        }
        i+=length;
    }
    return result;
}
}
