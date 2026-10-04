#include "formatted_text.hpp"
#include "unicode_text.hpp"
#include "unicode_services.hpp"
#include <algorithm>
#include <array>
#include <iterator>

namespace eu4unicode {
namespace {
bool icon_byte(unsigned char value) noexcept {
    return (value>='0'&&value<='9')||(value>='A'&&value<='Z')||
           (value>='a'&&value<='z')||value=='_';
}
Scalar native_scalar(std::string_view value) noexcept {
    auto scalar=decode(value);
    if(!scalar.valid&&!value.empty()) scalar.value=static_cast<unsigned char>(value.front());
    return scalar;
}
}
TextUnit native_text_unit(std::string_view text,std::size_t offset,bool formatted) {
    if(offset>=text.size()) return {TextUnitKind::glyph,0,text.size(),text.size()};
    const auto scalar=native_scalar(text.substr(offset));
    const auto end=offset+scalar.bytes;
    if(formatted&&scalar.value==0xa7&&end<text.size()&&static_cast<unsigned char>(text[end])<0x80)
        return {TextUnitKind::color,scalar.value,offset,end+1};
    if(formatted&&scalar.value==0xa3) {
        auto cursor=end;
        while(cursor<text.size()&&icon_byte(static_cast<unsigned char>(text[cursor]))) ++cursor;
        if(cursor>end&&cursor<text.size()) {
            const auto closing=native_scalar(text.substr(cursor));
            if(closing.value==0xa3)
                return {TextUnitKind::icon,0xfffc,offset,cursor+closing.bytes};
            if(closing.value==' '||closing.value=='\t'||closing.value==0xa0)
                return {TextUnitKind::icon,0xfffc,offset,cursor+closing.bytes};
        }
    }
    return {TextUnitKind::glyph,scalar.value,offset,end};
}
std::size_t native_scalar_start(std::string_view text,std::size_t offset) noexcept {
    if(offset>=text.size()) return text.size();
    auto start=offset;
    while(start&&offset-start<3&&(static_cast<unsigned char>(text[start])&0xc0)==0x80) --start;
    const auto scalar=decode(text.substr(start));
    return scalar.valid&&offset<start+scalar.bytes?start:offset;
}
std::size_t native_scalar_next(std::string_view text,std::size_t offset) noexcept {
    if(offset>=text.size()) return text.size();
    const auto scalar=native_scalar(text.substr(offset));
    return offset+scalar.bytes;
}
Scalar native_measure_scalar(std::string_view text) noexcept {
    if(text.empty()) return {0,0,false};
    const auto lead=static_cast<unsigned char>(text.front());
    const std::size_t expected=lead>=0xc2&&lead<=0xdf?2:
        lead>=0xe0&&lead<=0xef?3:lead>=0xf0&&lead<=0xf4?4:1;
    if(text.size()<expected&&std::all_of(text.begin()+1,text.end(),[](char value) {
        return (static_cast<unsigned char>(value)&0xc0)==0x80;
    })) return {0,0,false};
    return native_scalar(text);
}
FormattedText::FormattedText(std::string_view text,bool formatted) {
    struct Position { std::size_t visible,raw; };
    std::vector<Position> positions{{0,0}};
    for(std::size_t cursor=0;cursor<text.size();) {
        const auto unit=native_text_unit(text,cursor,formatted);
        if(unit.kind!=TextUnitKind::color) visible_+=encode(unit.scalar);
        positions.push_back({visible_.size(),unit.end});
        cursor=unit.end;
    }
    const auto graphemes=grapheme_boundaries(visible_);
    const auto breaks=line_boundaries(visible_);
    for(const auto& position:positions) {
        if(std::binary_search(graphemes.begin(),graphemes.end(),position.visible))
            prefixes_.push_back(position.raw);
        if(std::binary_search(breaks.begin(),breaks.end(),position.visible))
            lines_.push_back(position.raw);
    }
}
std::size_t FormattedText::prefix(std::size_t limit) const noexcept {
    const auto found=std::upper_bound(prefixes_.begin(),prefixes_.end(),limit);
    return found==prefixes_.begin()?0:*std::prev(found);
}
bool FormattedText::line_before(std::size_t offset) const noexcept {
    return std::binary_search(lines_.begin(),lines_.end(),offset);
}
std::size_t FormattedText::memory_size() const noexcept {
    return sizeof(*this)+visible_.capacity()+
        (prefixes_.capacity()+lines_.capacity())*sizeof(std::size_t);
}
bool layout_substring_caller(std::uintptr_t caller) noexcept {
    constexpr std::array<std::uintptr_t,11> callers{
        0x159f148,0x159f230,0x159f373,0x159fa18,0x159faec,0x159fc46,
        0x159fd48,0x159fe60,0x159feba,0x159ff80,0x15a00a0};
    return std::find(callers.begin(),callers.end(),caller)!=callers.end();
}
}
