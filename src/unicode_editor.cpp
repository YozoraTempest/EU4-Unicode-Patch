#include "unicode_editor.hpp"
#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <stdexcept>

namespace eu4unicode {
namespace {
std::size_t floor_boundary(const std::vector<std::size_t>& boundaries,std::size_t offset) {
    return *--std::upper_bound(boundaries.begin(),boundaries.end(),offset);
}
std::size_t ceil_boundary(const std::vector<std::size_t>& boundaries,std::size_t offset) {
    return *std::lower_bound(boundaries.begin(),boundaries.end(),offset);
}
}
std::size_t grapheme_prefix(std::string_view text,std::size_t byte_limit) {
    const auto boundaries=grapheme_boundaries(text);
    return floor_boundary(boundaries,(std::min)(text.size(),byte_limit));
}
std::string filter_editor_characters(std::string_view text,std::string_view blacklist) {
    if(!valid_utf8(text)) throw std::invalid_argument("Editor input is not valid UTF-8");
    std::string output;
    for(std::size_t offset=0;offset<text.size();) {
        const auto scalar=decode(text.substr(offset));
        const auto excluded=scalar.value<=0xff&&
            blacklist.find(static_cast<char>(scalar.value))!=std::string_view::npos;
        if(!excluded) output.append(text.substr(offset,scalar.bytes));
        offset+=scalar.bytes;
    }
    return output;
}
EditPlan plan_edit(std::string_view text,std::size_t caret,EditKey key) {
    if(caret>text.size()) throw std::out_of_range("Caret exceeds text size");
    const auto boundaries=grapheme_boundaries(text);
    const auto previous=std::lower_bound(boundaries.begin(),boundaries.end(),caret);
    const auto next=std::upper_bound(boundaries.begin(),boundaries.end(),caret);
    const auto left=previous==boundaries.begin()?0:*std::prev(previous);
    const auto right=next==boundaries.end()?text.size():*next;
    switch(key) {
    case EditKey::left: return {caret,caret,left};
    case EditKey::right: return {caret,caret,right};
    case EditKey::backspace: return {left,ceil_boundary(boundaries,caret),left};
    case EditKey::forward_delete: {
        const auto start=floor_boundary(boundaries,caret);
        return {start,right,start};
    }
    }
    throw std::invalid_argument("Unknown edit key");
}
Selection align_selection(std::string_view text,std::size_t anchor,std::size_t caret) {
    if(anchor>text.size()||caret>text.size()) throw std::out_of_range("Selection exceeds text size");
    const auto boundaries=grapheme_boundaries(text);
    const auto start=floor_boundary(boundaries,(std::min)(anchor,caret));
    const auto end=anchor==caret?start:ceil_boundary(boundaries,(std::max)(anchor,caret));
    return anchor<=caret?Selection{start,end}:Selection{end,start};
}
EditResult replace_selection(std::string_view text,std::size_t anchor,std::size_t caret,
    std::string_view insertion,std::size_t byte_limit) {
    if(!valid_utf8(insertion)) throw std::invalid_argument("Commit is not valid UTF-8");
    const auto selection=align_selection(text,anchor,caret);
    const auto start=(std::min)(selection.anchor,selection.caret);
    const auto end=(std::max)(selection.anchor,selection.caret);
    const auto retained=text.size()-(end-start);
    if(retained>byte_limit||insertion.size()>byte_limit-retained)
        throw std::length_error("Commit exceeds editor byte limit");
    std::string output(text.substr(0,start));
    output.append(insertion);
    output.append(text.substr(end));
    const auto resulting_boundaries=grapheme_boundaries(output);
    const auto position=ceil_boundary(resulting_boundaries,start+insertion.size());
    return {std::move(output),position};
}
}
