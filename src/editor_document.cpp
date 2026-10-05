#include "editor_document.hpp"
#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <stdexcept>

namespace eu4unicode {
EditRows::EditRows(std::string_view source,const std::vector<EditRowInput>& rows):bytes_(source.size()) {
    if(source.size()>32000||!valid_utf8(source)||rows.size()>500)
        throw std::invalid_argument("Invalid native editor document");
    std::size_t start=0;
    for(const auto& input:rows) {
        auto row=input.text;
        if(input.synthetic_newline) {
            if(row.empty()||row.back()!='\n') throw std::invalid_argument("Invalid native soft wrap");
            row.remove_suffix(1);
        }
        const auto segment=source.substr(start,row.size());
        const bool replaced_space=!input.synthetic_newline&&!row.empty()&&row.back()=='\n'&&
            segment.size()==row.size()&&segment.back()==' '&&segment.substr(0,segment.size()-1)==row.substr(0,row.size()-1);
        if(row.size()>source.size()-start||(segment!=row&&!replaced_space)||!valid_utf8(row))
            throw std::invalid_argument("Native editor rows do not match the document");
        const auto consumed=row.size();
        if(!row.empty()&&row.back()=='\n') { row.remove_suffix(1);if(!row.empty()&&row.back()=='\r') row.remove_suffix(1); }
        rows_.push_back({start,row.size(),consumed});start+=consumed;
    }
    if(start!=source.size()) throw std::invalid_argument("Native editor row cache is incomplete");
    if(rows_.empty()) rows_.push_back({0,0,0});
    // The native cache omits the empty row following a final hard newline.
    // Its absolute-offset routine accepts row == cache.size(), column == 0.
    if(!source.empty()&&source.back()=='\n'&&rows_.back().consumed>rows_.back().length)
        rows_.push_back({source.size(),0,0});
    const auto boundaries=grapheme_boundaries(source);
    for(const auto& row:rows_) if(!std::binary_search(boundaries.begin(),boundaries.end(),row.start)||
       !std::binary_search(boundaries.begin(),boundaries.end(),row.start+row.length))
        throw std::invalid_argument("Native editor row split a grapheme");
}
std::size_t EditRows::offset(EditPosition position) const {
    const auto& row=rows_.at(position.row);
    if(position.column>row.length) throw std::out_of_range("Editor column exceeds its row");
    return row.start+position.column;
}
EditPosition EditRows::position(std::size_t offset) const {
    if(offset>bytes_) throw std::out_of_range("Editor position exceeds document");
    const auto found=std::upper_bound(rows_.begin(),rows_.end(),offset,
        [](std::size_t value,const EditRow& row){return value<row.start;});
    const auto index=found==rows_.begin()?0u:static_cast<std::size_t>(std::prev(found)-rows_.begin());
    return {index,(std::min)(offset-rows_[index].start,rows_[index].length)};
}
namespace {
std::size_t cost(const EditState& state) { return sizeof(EditState)+state.text.size(); }
void validate(const EditState& state) {
    const auto aligned=align_selection(state.text,state.selection.anchor,state.selection.caret);
    if(state.text.size()>32000||aligned.anchor!=state.selection.anchor||aligned.caret!=state.selection.caret)
        throw std::invalid_argument("History state must contain complete graphemes");
}
}
void EditHistory::clear() noexcept { changes_.clear();index_=bytes_=0; }
bool EditHistory::synchronize(const EditState& current) {
    if(changes_.empty()) return false;
    const auto& expected=index_?changes_[index_-1].after:changes_.front().before;
    if(current.text==expected.text) return true;
    clear();return false;
}
void EditHistory::record(const EditState& before,const EditState& after) {
    if(before.text==after.text) return;
    validate(before);validate(after);
    if(!changes_.empty()) synchronize(before);
    while(changes_.size()>index_) {
        bytes_-=cost(changes_.back().before)+cost(changes_.back().after);changes_.pop_back();
    }
    changes_.push_back({before,after});++index_;bytes_+=cost(before)+cost(after);
    while(changes_.size()>128||bytes_>2*1024*1024) {
        bytes_-=cost(changes_.front().before)+cost(changes_.front().after);changes_.pop_front();--index_;
    }
}
std::optional<EditState> EditHistory::undo(const EditState& current) {
    if(!synchronize(current)||!index_) return {};
    return changes_[--index_].before;
}
std::optional<EditState> EditHistory::redo(const EditState& current) {
    if(!synchronize(current)||index_==changes_.size()) return {};
    return changes_[index_++].after;
}
CompositionText composition_text(std::u16string_view source,std::size_t caret) {
    if(source.size()>32000||caret>source.size()) throw std::invalid_argument("Invalid IME composition length");
    CompositionText result;
    for(std::size_t index=0;index<source.size();) {
        const auto start=index;
        std::uint32_t scalar=source[index++];
        if(scalar>=0xd800&&scalar<=0xdbff) {
            if(index==source.size()||source[index]<0xdc00||source[index]>0xdfff)
                throw std::invalid_argument("Invalid IME surrogate pair");
            scalar=0x10000+((scalar-0xd800)<<10)+(source[index++]-0xdc00);
        } else if(!scalar||(scalar>=0xdc00&&scalar<=0xdfff)) throw std::invalid_argument("Invalid IME scalar");
        if(caret>start&&caret<index) caret=start;
        if(index<=caret) result.caret=result.text.size()+encode(scalar).size();
        result.text+=encode(scalar);
    }
    result.caret=grapheme_prefix(result.text,result.caret);return result;
}
CompositionPreview composition_preview(const EditState& source,const CompositionText& composition,std::size_t budget) {
    const auto selection=align_selection(source.text,source.selection.anchor,source.selection.caret);
    const auto start=(std::min)(selection.anchor,selection.caret);
    auto replaced=replace_selection(source.text,selection.anchor,selection.caret,composition.text,budget);
    if(grapheme_prefix(composition.text,composition.caret)!=composition.caret)
        throw std::invalid_argument("IME caret splits a grapheme");
    const auto boundaries=grapheme_boundaries(replaced.text);
    const auto caret=*std::lower_bound(boundaries.begin(),boundaries.end(),start+composition.caret);
    const auto begin=grapheme_prefix(replaced.text,start);
    const auto end=*std::lower_bound(boundaries.begin(),boundaries.end(),start+composition.text.size());
    return {std::move(replaced.text),caret,begin,end};
}
}
