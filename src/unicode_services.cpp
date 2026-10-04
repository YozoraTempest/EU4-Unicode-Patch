#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include <icu.h>
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace eu4unicode {
namespace {
void checked(UErrorCode status) {
    if(U_FAILURE(status)) throw std::runtime_error(u_errorName(status));
}
void checked_text(std::string_view text) {
    if(text.size()>static_cast<std::size_t>((std::numeric_limits<int32_t>::max)()))
        throw std::length_error("Text exceeds ICU boundary offset range");
    if(!valid_utf8(text)) throw std::invalid_argument("Invalid UTF-8 text");
}
std::vector<std::size_t> boundaries(std::string_view text,UBreakIteratorType type) {
    checked_text(text);
    UErrorCode status=U_ZERO_ERROR;
    std::unique_ptr<UText,decltype(&utext_close)> source(utext_openUTF8(nullptr,text.data(),
        static_cast<int64_t>(text.size()),&status),utext_close);
    checked(status);
    std::unique_ptr<UBreakIterator,decltype(&ubrk_close)> iterator(
        ubrk_open(type,"",nullptr,0,&status),ubrk_close);
    checked(status);
    ubrk_setUText(iterator.get(),source.get(),&status);
    checked(status);
    std::vector<std::size_t> result;
    for(auto index=ubrk_first(iterator.get());index!=UBRK_DONE;index=ubrk_next(iterator.get()))
        result.push_back(static_cast<std::size_t>(index));
    return result;
}
}
std::vector<std::size_t> grapheme_boundaries(std::string_view text) { return boundaries(text,UBRK_CHARACTER); }
std::vector<std::size_t> line_boundaries(std::string_view text) { return boundaries(text,UBRK_LINE); }
std::size_t previous_grapheme(std::string_view text,std::size_t offset) {
    if(offset>text.size()) throw std::out_of_range("Caret exceeds text size");
    const auto positions=grapheme_boundaries(text);
    auto cursor=std::lower_bound(positions.begin(),positions.end(),offset);
    return cursor==positions.begin()?0:*--cursor;
}
std::size_t next_grapheme(std::string_view text,std::size_t offset) {
    if(offset>text.size()) throw std::out_of_range("Caret exceeds text size");
    const auto positions=grapheme_boundaries(text);
    const auto cursor=std::upper_bound(positions.begin(),positions.end(),offset);
    return cursor==positions.end()?text.size():*cursor;
}
namespace {
using NormalizerFactory=const UNormalizer2*(*)(UErrorCode*);
std::string normalize_text(std::string_view text,NormalizerFactory factory) {
    checked_text(text);
    if(text.empty()) return {};
    UErrorCode status=U_ZERO_ERROR;
    int32_t length=0;
    u_strFromUTF8(nullptr,0,&length,text.data(),static_cast<int32_t>(text.size()),&status);
    if(status!=U_BUFFER_OVERFLOW_ERROR) checked(status);
    status=U_ZERO_ERROR;
    std::u16string wide(static_cast<std::size_t>(length),u'\0');
    u_strFromUTF8(wide.data(),length,&length,text.data(),static_cast<int32_t>(text.size()),&status);
    checked(status);
    const auto normalizer=factory(&status);
    checked(status);
    length=unorm2_normalize(normalizer,wide.data(),static_cast<int32_t>(wide.size()),nullptr,0,&status);
    if(status!=U_BUFFER_OVERFLOW_ERROR) checked(status);
    status=U_ZERO_ERROR;
    std::u16string normalized(static_cast<std::size_t>(length),u'\0');
    unorm2_normalize(normalizer,wide.data(),static_cast<int32_t>(wide.size()),normalized.data(),length,&status);
    checked(status);
    int32_t bytes=0;
    status=U_ZERO_ERROR;
    u_strToUTF8(nullptr,0,&bytes,normalized.data(),length,&status);
    if(status!=U_BUFFER_OVERFLOW_ERROR) checked(status);
    status=U_ZERO_ERROR;
    std::string result(static_cast<std::size_t>(bytes),'\0');
    u_strToUTF8(result.data(),bytes,&bytes,normalized.data(),length,&status);
    checked(status);
    return result;
}
}
std::string search_key(std::string_view text) { return normalize_text(text,unorm2_getNFKCCasefoldInstance); }
std::string canonical_text(std::string_view text) { return normalize_text(text,unorm2_getNFCInstance); }
std::string decomposed_text(std::string_view text) { return normalize_text(text,unorm2_getNFDInstance); }
std::string transliterated_text(std::string_view text,const char* transform) {
    checked_text(text);
    using Transform=std::unique_ptr<UTransliterator,decltype(&utrans_close)>;
    thread_local std::unordered_map<std::string,Transform> transforms;
    auto found=transforms.find(transform);
    if(found==transforms.end()) {
        std::u16string id;
        for(const auto c:std::string_view(transform)) id.push_back(static_cast<char16_t>(c));
        UErrorCode status=U_ZERO_ERROR;
        Transform value(utrans_openU(id.data(),static_cast<int32_t>(id.size()),UTRANS_FORWARD,
            nullptr,0,nullptr,&status),utrans_close);
        checked(status);
        found=transforms.emplace(transform,std::move(value)).first;
    }
    if(text.empty()) return {};
    UErrorCode status=U_ZERO_ERROR;
    int32_t length=0;
    u_strFromUTF8(nullptr,0,&length,text.data(),static_cast<int32_t>(text.size()),&status);
    if(status!=U_BUFFER_OVERFLOW_ERROR) checked(status);
    const auto initial_length=length;
    std::size_t capacity=static_cast<std::size_t>(length)+32;
    for(;;) {
        if(capacity>static_cast<std::size_t>(INT32_MAX)) throw std::length_error("Transliteration exceeds ICU size range");
        std::u16string buffer(capacity,u'\0');
        status=U_ZERO_ERROR;
        u_strFromUTF8(buffer.data(),static_cast<int32_t>(capacity),&length,text.data(),
            static_cast<int32_t>(text.size()),&status);
        checked(status);
        int32_t limit=initial_length;
        utrans_transUChars(found->second.get(),buffer.data(),&length,static_cast<int32_t>(capacity),0,&limit,&status);
        if(status==U_BUFFER_OVERFLOW_ERROR) { capacity=(std::max)(capacity*2,static_cast<std::size_t>(length)+1); continue; }
        checked(status);
        int32_t bytes=0;
        status=U_ZERO_ERROR;
        u_strToUTF8(nullptr,0,&bytes,buffer.data(),length,&status);
        if(status!=U_BUFFER_OVERFLOW_ERROR) checked(status);
        status=U_ZERO_ERROR;
        std::string result(static_cast<std::size_t>(bytes),'\0');
        u_strToUTF8(result.data(),bytes,&bytes,buffer.data(),length,&status);
        checked(status);
        return result;
    }
}
}
