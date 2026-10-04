#include "shaped_paragraph.hpp"
#include "unicode_text.hpp"
#include <icu.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace eu4unicode {
bool needs_paragraph_shaping(std::string_view text) noexcept {
    bool contextual=false;
    while(!text.empty()) {
        const auto scalar=decode(text);
        if(!scalar.valid) return false;
        if(scalar.value<0x80||contextual) { text.remove_prefix(scalar.bytes);continue; }
        const auto cp=static_cast<UChar32>(scalar.value);
        const auto category=u_charType(cp);
        const auto direction=u_charDirection(cp);
        contextual=contextual||category==U_NON_SPACING_MARK||category==U_COMBINING_SPACING_MARK||
            category==U_ENCLOSING_MARK||direction==U_RIGHT_TO_LEFT||direction==U_RIGHT_TO_LEFT_ARABIC||
            u_hasBinaryProperty(cp,UCHAR_BIDI_CONTROL)||u_hasBinaryProperty(cp,UCHAR_VARIATION_SELECTOR)||
            u_hasBinaryProperty(cp,UCHAR_EMOJI_MODIFIER)||cp==0x200c||cp==0x200d;
        UErrorCode status=U_ZERO_ERROR;
        switch(uscript_getScript(cp,&status)) {
        case USCRIPT_COMMON:case USCRIPT_INHERITED:case USCRIPT_LATIN:case USCRIPT_GREEK:
        case USCRIPT_CYRILLIC:case USCRIPT_HAN:case USCRIPT_HANGUL:case USCRIPT_HIRAGANA:
        case USCRIPT_KATAKANA:case USCRIPT_BOPOMOFO:break;
        default:contextual=true;break;
        }
        if(U_FAILURE(status)) return false;
        text.remove_prefix(scalar.bytes);
    }
    return contextual;
}
bool plain_native_paragraph(std::string_view text) noexcept {
    if(text.empty()||text.size()>32000||!valid_utf8(text)) return false;
    while(!text.empty()) {
        const auto scalar=decode(text);
        // These are native format commands, not plain glyphs in the main UI.
        if(scalar.value==0||scalar.value==0xa7||scalar.value==0xa3||scalar.value==0xa4||
           scalar.value=='@'||scalar.value=='{'||scalar.value=='\t') return false;
        text.remove_prefix(scalar.bytes);
    }
    return true;
}
ShapedParagraph::ShapedParagraph(std::string_view text,int size,float width,bool wrap,
                               std::shared_ptr<const TextFonts> fonts):text_(text) {
    if(text.size()>32000||size<=0||size>512||!std::isfinite(width)||width<=0||width>32767)
        throw std::invalid_argument("Native paragraph exceeds layout bounds");
    if(!valid_utf8(text)) throw std::invalid_argument("Paragraph must be valid UTF-8");
    std::size_t start=0;
    for(;;) {
        auto content_end=start;std::size_t newline=0;
        while(content_end<text.size()) {
            const auto scalar=decode(text.substr(content_end));
            if(scalar.value=='\r'||scalar.value=='\n'||scalar.value==0x85||scalar.value==0x2028||scalar.value==0x2029) {
                newline=scalar.bytes;
                if(scalar.value=='\r'&&content_end+1<text.size()&&text[content_end+1]=='\n') ++newline;
                break;
            }
            content_end+=scalar.bytes;
        }
        auto layout=std::make_unique<TextLayout>(text.substr(start,content_end-start),static_cast<float>(size),width,
            static_cast<float>(size)*501,L"Microsoft YaHei UI",fonts,
            TextLayoutOptions{TextDirection::Automatic,wrap,static_cast<float>(size)});
        const auto metrics=layout->metrics();
        auto lines=layout->lines();
        if(lines_.size()+lines.size()>500) throw std::length_error("Native paragraph exceeds line capacity");
        if(metrics.width>32767) throw std::length_error("Native paragraph exceeds advance capacity");
        auto runs=layout->glyph_runs();
        for(auto& run:runs) {
            run.text_start+=start;run.baseline_y+=metrics_.height;
            for(auto& cluster:run.clusters) cluster.text_start+=start;
            runs_.push_back(std::move(run));
        }
        for(auto& line:lines) { line.text_start+=start;line.top+=metrics_.height; }
        if(!lines.empty()) { lines.back().text_length+=newline;lines.back().newline_length+=newline; }
        lines_.insert(lines_.end(),lines.begin(),lines.end());
        blocks_.push_back({start,metrics_.height,metrics.height,std::move(layout)});
        metrics_.width=(std::max)(metrics_.width,metrics.width);
        metrics_.height+=metrics.height;
        if(!newline) break;
        start=content_end+newline;
    }
    metrics_.lines=static_cast<std::uint32_t>(lines_.size());
}
HitPosition ShapedParagraph::hit_test(float x,float y) const {
    if(!std::isfinite(x)||!std::isfinite(y)) throw std::invalid_argument("Hit coordinates must be finite");
    const auto found=std::upper_bound(blocks_.begin(),blocks_.end(),y,
        [](float value,const Block& block){return value<block.top;});
    const auto& block=found==blocks_.begin()?blocks_.front():*std::prev(found);
    auto hit=block.layout->hit_test(x,y-block.top);hit.byte_offset+=block.start;
    hit.inside=hit.inside&&y>=block.top&&y<block.top+block.height;
    return hit;
}
bool ShapedParagraph::missing_glyphs() const noexcept {
    for(const auto& run:runs_) for(const auto& cluster:run.clusters) {
        bool missing=false;
        for(auto index=cluster.first_glyph;index<cluster.first_glyph+cluster.glyph_count;++index)
            missing=missing||run.glyphs[index]==0;
        if(!missing) continue;
        auto source=std::string_view(text_).substr(cluster.text_start,cluster.text_length);
        while(!source.empty()) {
            const auto scalar=decode(source);
            if(!u_hasBinaryProperty(static_cast<UChar32>(scalar.value),UCHAR_DEFAULT_IGNORABLE_CODE_POINT)) return true;
            source.remove_prefix(scalar.bytes);
        }
    }
    return false;
}
std::vector<ParagraphTile> rasterize_paragraph(const ShapedParagraph& paragraph,std::uint32_t width,std::uint32_t height) {
    if(!width||!height||width>16384||height>16384) throw std::invalid_argument("Invalid paragraph tile dimensions");
    std::vector<ParagraphTile> result;
    std::size_t bytes=0;
    for(const auto& run:paragraph.runs()) {
        const auto raster=rasterize_glyph_run(run);
        const auto& lines=paragraph.lines();
        const auto found=std::upper_bound(lines.begin(),lines.end(),run.baseline_y,
            [](float baseline,const LayoutLine& line){return baseline<line.top;});
        const auto line=found==lines.begin()?0u:static_cast<std::size_t>(std::prev(found)-lines.begin());
        for(std::uint32_t y=0;y<raster.height;y+=height) for(std::uint32_t x=0;x<raster.width;x+=width) {
            const auto w=(std::min)(width,raster.width-x),h=(std::min)(height,raster.height-y);
            bytes+=static_cast<std::size_t>(w)*h;
            if(bytes>16ull*1024*1024) throw std::length_error("Paragraph raster exceeds CPU budget");
            GlyphBitmap tile{w,h,0,0,{}};tile.alpha.resize(static_cast<std::size_t>(w)*h);
            for(std::uint32_t row=0;row<h;++row)
                std::copy_n(raster.alpha.data()+static_cast<std::size_t>(y+row)*raster.width+x,w,
                    tile.alpha.data()+static_cast<std::size_t>(row)*w);
            result.push_back({std::move(tile),std::floor(run.baseline_x)+raster.left+x,
                std::floor(run.baseline_y)+raster.top+y,line,run.text_start,run.text_length});
        }
    }
    return result;
}
}
