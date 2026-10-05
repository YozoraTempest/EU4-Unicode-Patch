#include "shaped_paragraph.hpp"
#include "unicode_text.hpp"
#include "unicode_services.hpp"
#include "formatted_paragraph.hpp"
#include <icu.h>
#include <algorithm>
#include <cmath>
#include <limits>
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
                               std::shared_ptr<const TextFonts> fonts,std::shared_ptr<const ParagraphText> content):text_(text),content_(std::move(content)) {
    if(text.size()>32000||size<=0||size>512||!std::isfinite(width)||width<=0||width>32767)
        throw std::invalid_argument("Native paragraph exceeds layout bounds");
    if(!content_) content_=std::make_shared<ParagraphText>(text,false);
    if(content_->source()!=text_) throw std::invalid_argument("Paragraph source mapping belongs to different text");
    text=content_->visible();
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
        TextLayoutOptions options{TextDirection::Automatic,wrap,static_cast<float>(size)};
        for(const auto& style:content_->styles()) {
            const auto first=(std::max)(style.text_start,start),last=(std::min)(style.text_start+style.text_length,content_end);
            if(first<last) options.styles.push_back({first-start,last-first,style.style});
        }
        for(std::size_t index=0;index<content_->icons().size();++index) {
            const auto& icon=content_->icons()[index];
            if(icon.text_start>=start&&icon.text_start<content_end)
                options.objects.push_back({icon.text_start-start,icon.text_length,icon.advance,static_cast<float>(size),size*.8f,static_cast<std::uint32_t>(index)});
        }
        auto layout=std::make_unique<TextLayout>(text.substr(start,content_end-start),static_cast<float>(size),width,
            static_cast<float>(size)*501,L"Microsoft YaHei UI",fonts,
            std::move(options));
        const auto metrics=layout->metrics();
        auto lines=layout->lines();
        if(lines_.size()+lines.size()>500) throw std::length_error("Native paragraph exceeds line capacity");
        if(metrics.width>32767) throw std::length_error("Native paragraph exceeds advance capacity");
        auto drawing=layout->drawing();
        auto source_range=[&](std::size_t& position,std::size_t& length) {
            const auto first=content_->source_byte(start+position),last=content_->source_byte(start+position+length);
            position=first;length=last-first;
        };
        for(auto& run:drawing.runs) {
            for(auto& cluster:run.clusters) {
                bool absent=false;
                for(auto index=cluster.first_glyph;index<cluster.first_glyph+cluster.glyph_count;++index)
                    absent=absent||run.glyphs[index]==0;
                if(absent) {
                    auto source=text.substr(start+cluster.text_start,cluster.text_length);
                    while(!source.empty()) {
                        const auto scalar=decode(source);
                        missing_=missing_||!u_hasBinaryProperty(static_cast<UChar32>(scalar.value),UCHAR_DEFAULT_IGNORABLE_CODE_POINT);
                        source.remove_prefix(scalar.bytes);
                    }
                }
                source_range(cluster.text_start,cluster.text_length);
            }
            source_range(run.text_start,run.text_length);run.baseline_y+=metrics_.height;
            runs_.push_back(std::move(run));
        }
        if(!lines.empty()) { lines.back().text_length+=newline;lines.back().newline_length+=newline; }
        for(auto& line:lines) {
            const auto end=line.text_start+line.text_length;
            line.newline_length=content_->source_byte(start+end)-content_->source_byte(start+end-line.newline_length);
            source_range(line.text_start,line.text_length);line.top+=metrics_.height;
        }
        for(auto& object:drawing.objects) {
            source_range(object.text_start,object.text_length);object.y+=metrics_.height;
            objects_.push_back(object);
        }
        lines_.insert(lines_.end(),lines.begin(),lines.end());
        blocks_.push_back({start,content_end-start,metrics_.height,metrics.height,std::move(layout)});
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
    auto hit=block.layout->hit_test(x,y-block.top);hit.byte_offset=content_->source_byte(hit.byte_offset+block.start);
    hit.inside=hit.inside&&y>=block.top&&y<block.top+block.height;
    return hit;
}
bool ShapedParagraph::missing_glyphs() const noexcept {
    return missing_;
}
CaretPosition ShapedParagraph::caret(std::size_t offset,bool trailing) const {
    const auto visible=content_->visible_byte(offset);
    auto found=std::upper_bound(blocks_.begin(),blocks_.end(),visible,
        [](std::size_t value,const Block& block){return value<block.start;});
    const auto& block=found==blocks_.begin()?blocks_.front():*std::prev(found);
    const auto local=(std::min)(visible-block.start,block.length);
    auto result=block.layout->caret(local,trailing);
    result.byte_offset=offset;result.y+=block.top;return result;
}
CaretPosition ShapedParagraph::move_caret(std::size_t offset,bool trailing,bool right) const {
    const auto current=caret(offset,trailing);
    auto result=current;
    float distance=(std::numeric_limits<float>::max)();
    std::call_once(caret_stops_once_,[this] {
        std::vector<CaretPosition> stops;
        for(const auto boundary:grapheme_boundaries(content_->visible())) for(const auto affinity:{false,true})
            stops.push_back(caret(content_->source_byte(boundary),affinity));
        caret_stops_=std::move(stops);
    });
    for(const auto& candidate:caret_stops_) {
        if(std::abs(candidate.y-current.y)>.1f) continue;
        const auto delta=right?candidate.x-current.x:current.x-candidate.x;
        if(delta>.1f&&delta<distance) { distance=delta;result=candidate; }
    }
    return result;
}
std::vector<SelectionRegion> ShapedParagraph::selection(std::size_t begin,std::size_t end) const {
    if(begin>end||end>text_.size()) throw std::out_of_range("Paragraph selection exceeds text");
    const auto first=content_->visible_byte(begin),last=content_->visible_byte(end);
    std::vector<SelectionRegion> result;
    for(const auto& block:blocks_) {
        const auto finish=block.start+block.length;
        const auto from=(std::max)(first,block.start),to=(std::min)(last,finish);
        if(from>=to) continue;
        for(auto box:block.layout->selection(from-block.start,to-block.start)) {
            const auto start=content_->source_byte(box.text_start+block.start);
            const auto finish=content_->source_byte(box.text_start+box.text_length+block.start);
            box.text_start=start;box.text_length=finish-start;box.y+=block.top;result.push_back(box);
        }
    }
    return result;
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
                std::floor(run.baseline_y)+raster.top+y,line,run.text_start,run.text_length,run.style});
        }
    }
    return result;
}
}
