#include "linux/paragraph_fit.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace eu4unicode::linux_native {
void align_font_substring(const FormattedText& text,std::size_t& begin,std::size_t& end) {
    begin=text.prefix(begin);end=std::max(begin,text.prefix(end));
}
std::optional<ParagraphFit> fit_paragraph(void* font,std::string_view source,
    int width,int height,const unsigned* padding,bool formatted,bool break_lines,bool ellipsis) {
    if(!font||!padding) return {};
    float scale=0;int size=0;
    std::memcpy(&scale,static_cast<const char*>(font)+0x940,sizeof(scale));
    std::memcpy(&size,static_cast<const char*>(font)+0x938,sizeof(size));
    const auto pixels=static_cast<double>(width?width:320)-2.*padding[0];
    if(!std::isfinite(scale)||scale<=0||size<=0||pixels<=0||pixels/scale>32767) return {};
    const auto paragraph=font_paragraph_layout(font,source,static_cast<float>(pixels/scale),true,formatted);
    if(!paragraph) return {};
    const auto line_height=static_cast<int>(size*scale);
    if(line_height<=0) throw std::invalid_argument("Invalid native paragraph line height");
    const auto available=static_cast<double>(height)-2.*padding[1];
    const auto capacity=static_cast<std::size_t>(std::max(1.,std::floor(available/line_height)));
    const auto count=std::min(capacity,paragraph->lines().size());
    const auto clipped=count<paragraph->lines().size();
    const auto attempted=(count+(clipped?1:0))*static_cast<double>(line_height)+2.*padding[1];
    if(attempted>std::numeric_limits<int>::max()) throw std::length_error("Native paragraph height exceeds capacity");
    ParagraphFit result{{},static_cast<int>(attempted),count,clipped,false};
    if(break_lines) {
        for(std::size_t index=0;index<count;++index) {
            const auto& line=paragraph->lines()[index];
            result.text.append(source.substr(line.text_start,line.text_length-line.newline_length));
            if(index+1<count||clipped) result.text+='\n';
        }
        return result;
    }
    if(!clipped) {result.text=source;return result;}
    const auto& last=paragraph->lines()[count-1];
    auto end=last.text_start+last.text_length-last.newline_length;
    const FormattedText boundaries(source,formatted);
    std::size_t begin=0;align_font_substring(boundaries,begin,end);
    if(!ellipsis) {result.text=source.substr(0,end);return result;}
    // Re-shape every candidate: removing a suffix can change Arabic joining,
    // bidi order and wrapping. A width sum of isolated glyphs is not valid.
    const auto& prefixes=boundaries.prefixes();
    auto cursor=std::upper_bound(prefixes.begin(),prefixes.end(),end);
    while(cursor!=prefixes.begin()) {
        end=*--cursor;
        result.text=source.substr(0,end);result.text+=end?" ...":"...";
        const auto fitted=font_paragraph_layout(font,result.text,static_cast<float>(pixels/scale),true,formatted,true);
        bool inside=fitted&&fitted->lines().size()<=count;
        if(inside) for(std::size_t line=0;line<fitted->lines().size();++line)
            inside=inside&&plan_paragraph_line(*fitted,line,scale).pixels<=pixels;
        if(inside) {result.ellipsis=true;return result;}
    }
    // The original fitting entry also emits the marker in an undersized box.
    result.text="...";result.ellipsis=true;return result;
}
}
