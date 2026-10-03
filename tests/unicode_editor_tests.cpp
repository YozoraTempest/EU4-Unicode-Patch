#include "unicode_editor.hpp"
#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

void check(bool condition,const char* message) {
    if(!condition) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
}
int main() {
    using namespace eu4unicode;
    const std::string text=u8"Ae\u0301中𠀀🇨🇳👩‍👩‍👧‍👦Z";
    const auto boundaries=grapheme_boundaries(text);
    for(std::size_t budget=0;budget<=text.size()+1;++budget) {
        const auto prefix=grapheme_prefix(text,budget);
        check(prefix<=budget&&std::binary_search(boundaries.begin(),boundaries.end(),prefix),
            "native budgets preserve complete graphemes");
        check(valid_utf8(text.substr(0,prefix)),"bounded editor text remains valid UTF-8");
    }
    check(grapheme_prefix(u8"Ae\u0301Z",2)==1,"budget cannot detach a combining mark");
    check(grapheme_prefix(u8"A👩‍👩‍👧‍👦Z",16)==1,"budget cannot retain half a ZWJ sequence");
    bool invalid_prefix_rejected=false;
    try { grapheme_prefix("\xf0\x9f",2); } catch(const std::invalid_argument&) { invalid_prefix_rejected=true; }
    check(invalid_prefix_rejected,"invalid input cannot be certified as a grapheme prefix");
    check(filter_editor_characters(u8"A代俧👧§Z","\xa7")==u8"A代俧👧Z",
        "native section-sign blacklist excludes the scalar and preserves continuation-byte collisions");
    check(filter_editor_characters(u8"中文A\nB","A\n")==u8"中文B","ASCII blacklist retains its native behavior");
    for(std::size_t caret=0;caret<=text.size();++caret) {
        for(const auto key:{EditKey::left,EditKey::right,EditKey::backspace,EditKey::forward_delete}) {
            const auto plan=plan_edit(text,caret,key);
            check(std::binary_search(boundaries.begin(),boundaries.end(),plan.caret),"every native byte position yields a whole-grapheme caret");
            if(key==EditKey::backspace||key==EditKey::forward_delete) {
                check(std::binary_search(boundaries.begin(),boundaries.end(),plan.erase_begin)&&
                    std::binary_search(boundaries.begin(),boundaries.end(),plan.erase_end),"deletion never leaves part of a grapheme");
                auto copy=text;
                copy.erase(plan.erase_begin,plan.erase_end-plan.erase_begin);
                check(valid_utf8(copy),"all native interior-byte deletion positions preserve UTF-8");
            }
        }
    }
    for(const auto key:{EditKey::backspace,EditKey::forward_delete}) {
        const auto plan=plan_edit(u8"中A",1,key);
        check(plan.erase_begin==0&&plan.erase_end==3,"interior Chinese caret deletes the complete containing character");
    }
    auto replaced=replace_selection(u8"A𠀀e\u0301Z",2,6,u8"中文",100);
    check(replaced.text==u8"A中文Z"&&replaced.caret==7,"selection expands to whole supplementary and combining graphemes");
    for(std::size_t anchor=0;anchor<=text.size();++anchor) {
        for(std::size_t caret=0;caret<=text.size();++caret) {
            const auto aligned=align_selection(text,anchor,caret);
            check(std::binary_search(boundaries.begin(),boundaries.end(),aligned.anchor)&&
                std::binary_search(boundaries.begin(),boundaries.end(),aligned.caret),
                "selection endpoints cannot divide scalars or graphemes");
            if(anchor<caret) check(aligned.anchor<=anchor&&aligned.caret>=caret,"forward selection includes all touched graphemes");
            if(anchor>caret) check(aligned.anchor>=anchor&&aligned.caret<=caret,"reverse selection keeps direction and all touched graphemes");
            if(anchor==caret) check(aligned.anchor==aligned.caret&&aligned.caret<=caret,"collapsed selection stays collapsed");
        }
    }
    replaced=replace_selection(u8"AeZ",2,2,u8"\u0301",100);
    check(replaced.text==u8"Ae\u0301Z"&&replaced.caret==4,"new combining input joins the preceding grapheme");
    replaced=replace_selection(u8"A𠀀Z",3,3,"B",100);
    check(replaced.text==u8"AB𠀀Z"&&replaced.caret==2,"collapsed interior caret snaps before the complete character");
    bool rejected=false;
    try { replace_selection("A",1,1,u8"𠀀",4); } catch(const std::length_error&) { rejected=true; }
    check(rejected,"over-budget commit cannot insert a partial four-byte character");
    rejected=false;
    try { replace_selection("A",1,1,"\xf0\x9f",100); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"truncated input cannot enter editor state");
    check(plan_edit("",0,EditKey::backspace).caret==0,"empty editor edge");
    std::cout<<"UTF-8 editor interior-byte, selection, commit and budget checks passed.\n";
}
