#include "formatted_paragraph.hpp"
#include "formatted_text.hpp"
#include "shaped_paragraph.hpp"
#include "unicode_services.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace eu4unicode {
bool needs_native_paragraph_shaping(std::string_view source,bool formatted) noexcept {
    if(source.empty()||source.size()>32000) return false;
    if(!formatted||valid_utf8(source)) return needs_paragraph_shaping(source);
    // Compiled native literals may use single-byte format delimiters. Visible
    // text still has to be UTF-8; no legacy localization decoding occurs here.
    try {
        std::string visible;
        for(std::size_t offset=0;offset<source.size();) {
            const auto unit=native_text_unit(source,offset,true);
            if(unit.kind==TextUnitKind::glyph) {
                const auto scalar=decode(source.substr(offset));
                if(!scalar.valid) return false;
                visible.append(source.substr(offset,unit.end-offset));
            }
            offset=unit.end;
        }
        return needs_paragraph_shaping(visible);
    } catch(...) { return false; }
}
ParagraphText::ParagraphText(std::string_view source,bool formatted,IconMeasure measure,ColorLookup lookup,IconMeasure flags,BitmapMeasure bitmap,IconMeasure symbols):source_(source) {
    if(source.size()>32000) throw std::length_error("Formatted paragraph exceeds source capacity");
    struct Unit { std::size_t start,end;std::uint32_t style; };
    std::vector<Unit> units;
    std::string stack;
    source_bytes_.push_back(0);
    for(std::size_t offset=0;offset<source.size();) {
        const auto unit=native_text_unit(source,offset,formatted);
        if(unit.kind==TextUnitKind::color) {
            const auto code=static_cast<unsigned char>(source[unit.end-1]);
            if(code=='!'&&!stack.empty()) stack.pop_back();
            else if(!lookup||lookup(code)) {
                if(stack.size()>=128) throw std::length_error("Paragraph color nesting exceeds capacity");
                stack+=static_cast<char>(code);
            }
            offset=unit.end;continue;
        }
        const auto style=std::find(colors_.begin(),colors_.end(),stack);
        const auto id=static_cast<std::uint32_t>(style-colors_.begin());
        if(style==colors_.end()) colors_.push_back(stack);
        const auto start=visible_.size();
        std::string glyph;
        if(unit.kind==TextUnitKind::icon||unit.kind==TextUnitKind::flag||unit.kind==TextUnitKind::symbol) {
            const auto& resolver=unit.kind==TextUnitKind::flag?flags:unit.kind==TextUnitKind::symbol?symbols:measure;
            if(!resolver) throw std::invalid_argument("Native inline object metrics are required");
            const auto command=source.substr(offset,unit.end-offset);
            const auto first=decode(command).bytes;
            const auto last=unit.kind==TextUnitKind::flag?command.size():native_scalar_start(command,command.size()-1);
            if(unit.kind!=TextUnitKind::symbol&&(last<=first||last-first>127)) throw std::length_error("Native icon name exceeds capacity");
            const auto advance=resolver(unit.kind==TextUnitKind::symbol?command:command.substr(first,last-first));
            if(!std::isfinite(advance)||advance<0||advance>32767)
                throw std::invalid_argument("Invalid native icon advance");
            glyph=encode(0xfffc);
            icons_.push_back({start,glyph.size(),std::string(command),advance});
        } else {
            const auto scalar=decode(source.substr(offset));
            if(!scalar.valid||!scalar.value||scalar.value=='\t'||(formatted&&
                (scalar.value==0xa7||scalar.value==0xa3||scalar.value==0xa4||
                 scalar.value=='@'||scalar.value=='{'||scalar.value=='$')))
                throw std::invalid_argument("Unsupported native paragraph command or encoding");
            glyph=std::string(source.substr(offset,scalar.bytes));
        }
        visible_+=glyph;
        for(std::size_t byte=1;byte<=glyph.size();++byte)
            source_bytes_.push_back(byte==glyph.size()?unit.end:offset);
        units.push_back({start,visible_.size(),id});
        offset=unit.end;
    }
    source_bytes_.back()=source.size();
    // A combining sequence has one drawing style, taken from its first scalar.
    // Color changes inside the cluster take effect at the next grapheme.
    const auto boundaries=grapheme_boundaries(visible_);
    std::size_t unit=0;
    for(std::size_t index=1;index<boundaries.size();++index) {
        const auto start=boundaries[index-1],end=boundaries[index];
        while(unit<units.size()&&units[unit].end<=start) ++unit;
        if(unit==units.size()) throw std::runtime_error("Invalid paragraph source mapping");
        const auto id=units[unit].style;
        const auto cluster=std::string_view(visible_).substr(start,end-start);
        const auto scalar=decode(cluster);
        if(bitmap&&scalar.bytes==cluster.size()&&scalar.value!=0xfffc&&scalar.value>=0x20&&
           scalar.value!=0x85&&scalar.value!=0x2028&&scalar.value!=0x2029&&!needs_paragraph_shaping(cluster)) {
            const auto advance=bitmap(scalar.value);
            if(std::isfinite(advance)&&advance>=0&&advance<=32767)
                icons_.push_back({start,end-start,std::string(cluster),advance});
        }
        if(!styles_.empty()&&styles_.back().style==id&&styles_.back().text_start+styles_.back().text_length==start)
            styles_.back().text_length+=end-start;
        else styles_.push_back({start,end-start,id});
    }
}
std::size_t ParagraphText::source_byte(std::size_t visible_byte) const { return source_bytes_.at(visible_byte); }
std::size_t ParagraphText::visible_byte(std::size_t source_byte) const {
    if(source_byte>source_.size()) throw std::out_of_range("Paragraph source position exceeds text");
    const auto found=std::lower_bound(source_bytes_.begin(),source_bytes_.end(),source_byte);
    if(found==source_bytes_.end()||*found!=source_byte)
        throw std::invalid_argument("Paragraph source position must be a text boundary");
    const auto byte=static_cast<std::size_t>(found-source_bytes_.begin());
    return byte==visible_.size()?byte:native_scalar_start(visible_,byte);
}
std::string paragraph_color_transition(std::string_view previous,std::string_view next) {
    std::size_t common=0;
    while(common<previous.size()&&common<next.size()&&previous[common]==next[common]) ++common;
    std::string result;
    for(auto index=common;index<previous.size();++index) result+=u8"§!";
    for(auto index=common;index<next.size();++index) { result+=u8"§";result+=next[index]; }
    return result;
}
}
