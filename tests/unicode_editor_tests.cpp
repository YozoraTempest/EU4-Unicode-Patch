#include "unicode_editor.hpp"
#include "unicode_services.hpp"
#include "unicode_text.hpp"
#include "editor_document.hpp"
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
    const std::string measured=u8"A中𠀀e\u0301🇨🇳👩‍👩‍👧‍👦Z";
    const auto edges=grapheme_boundaries(measured);
    const std::vector<int> widths={0,7,25,43,51,70,96,103};
    PrefixMeasure measure=[&](std::size_t prefix) {
        const auto found=std::lower_bound(edges.begin(),edges.end(),prefix);
        check(found!=edges.end()&&*found==prefix,"native measurement receives only complete graphemes");
        return widths[static_cast<std::size_t>(found-edges.begin())];
    };
    for(int pixel=-1;pixel<=110;++pixel) {
        std::size_t fits=0,nearest=0;
        for(std::size_t i=0;i<edges.size();++i) {
            if(widths[i]<=pixel) fits=edges[i];
            if(std::abs(widths[i]-pixel)<std::abs(widths[nearest]-pixel)) nearest=i;
        }
        check(fitting_grapheme_prefix(measured,pixel,measure)==fits,"pixel fitting matches complete measured prefixes");
        check(nearest_grapheme_boundary(measured,pixel,measure)==edges[nearest],"pixel hits match the closest complete edge, including ties");
    }
    check(fitting_grapheme_prefix("",10,measure)==0&&nearest_grapheme_boundary("",10,measure)==0,"empty measured editor has one edge");
    const std::string document=u8"中文 العربية\r\nहिन्दी English\n𠮷e\u0301";
    const std::vector<std::string> native_rows={u8"中文 \n",u8"العربية\r\n",u8"हिन्दी \n","English\n",u8"𠮷e\u0301"};
    std::vector<EditRowInput> input;
    for(std::size_t i=0;i<native_rows.size();++i) input.push_back({native_rows[i],i==0||i==2});
    const EditRows rows(document,input);
    for(std::size_t i=0;i<rows.rows().size();++i) for(const auto edge:grapheme_boundaries(std::string_view(document).substr(rows.rows()[i].start,rows.rows()[i].length))) {
        const auto offset=rows.offset({i,edge});const auto position=rows.position(offset);
        check(rows.offset(position)==offset,"soft wraps and hard CR/LF breaks preserve document byte positions");
    }
    check(rows.rows().size()==5&&rows.rows()[1].consumed==rows.rows()[1].length+2,"real CR/LF remains source bytes while synthetic breaks do not");
    const EditRows spaces(u8"中文 English العربية",{{u8"中文\n",false},{"English\n",false},{u8"العربية",false}});
    check(spaces.rows()[1].start==7&&spaces.rows()[2].start==15&&spaces.offset({1,0})==7,
        "native soft wraps replace consumed spaces without shifting source byte positions");
    const EditRows trailing("A\n\n",{{"A\n",false},{"\n",false}});
    check(trailing.rows().size()==3&&trailing.position(3).row==2&&trailing.offset({2,0})==3,
        "a final hard newline has an empty caret row even when the native cache omits it");
    rejected=false;
    try { EditRows bad(u8"e\u0301",{{"e\n",true},{u8"\u0301",false}}); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"native soft wraps cannot detach combining marks");
    EditHistory history;
    EditState empty{"",{0,0}},chinese{u8"中文",{6,6}},multiline{document,{0,document.size()}};
    history.record(empty,chinese);history.record(chinese,multiline);
    auto undone=history.undo(multiline);check(undone&&undone->text==chinese.text,"undo restores an entire multiline edit atomically");
    auto redone=history.redo(*undone);check(redone&&redone->text==document&&redone->selection.anchor==0,"redo restores text and selection direction");
    undone=history.undo(*redone);history.record(*undone,empty);
    check(!history.redo(empty),"a new edit discards the redo branch");
    check(!history.undo({"external",{0,0}})&&history.bytes()==0,"external game changes discard stale undo history");
    EditState before{std::string(31000,'a'),{0,0}};
    for(int i=0;i<500;++i) {
        auto after=before;after.text+='b';history.record(before,after);before=std::move(after);
        check(history.bytes()<=2*1024*1024,"undo history has a bounded storage budget");
    }
    unsigned retained=0;
    while(auto previous=history.undo(before)) { before=std::move(*previous);++retained; }
    check(retained>0&&retained<128,"large consecutive edits evict old states within the byte budget");
    const auto composed=composition_text(u"𠮷e\u0301 العربية हिन्दी",4);
    check(composed.text==u8"𠮷e\u0301 العربية हिन्दी"&&composed.caret==7,"IMM UTF-16 cursor maps to a complete UTF-8 grapheme");
    check(composition_text(u"𠮷",1).caret==0,"IME cursor cannot split a surrogate pair");
    check(composition_text(u"e\u0301",1).caret==0,"IME cursor cannot detach a combining mark");
    const auto preview=composition_preview({u8"A中文Z",{1,7}},composed);
    check(preview.text=="A"+composed.text+"Z"&&preview.caret==8&&preview.begin==1,"preedit previews replace the selection without modifying the source");
    rejected=false;try { composition_text(std::u16string(1,0xd800),0); } catch(const std::invalid_argument&) { rejected=true; }
    check(rejected,"incomplete IME surrogate input cannot enter presentation state");
    rejected=false;try { composition_preview(chinese,composed,6); } catch(const std::length_error&) { rejected=true; }
    check(rejected,"over-budget preedit leaves committed text intact");
    std::cout<<"UTF-8 editor interior-byte, selection, commit and budget checks passed.\n";
}
