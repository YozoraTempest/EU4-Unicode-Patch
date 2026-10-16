#include "paragraph_transport.hpp"
#include "formatted_paragraph.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace eu4unicode {
NativeLineAdvance plan_paragraph_line(const ShapedParagraph& paragraph,std::size_t line,float scale) {
    if(!std::isfinite(scale)||scale<=0) throw std::invalid_argument("Invalid paragraph advance scale");
    const auto& metrics=paragraph.lines().at(line);
    std::vector<const InlinePlacement*> objects;
    for(const auto& object:paragraph.objects())
        if(object.text_start>=metrics.text_start&&object.text_start<metrics.text_start+metrics.text_length)
            objects.push_back(&object);
    std::stable_sort(objects.begin(),objects.end(),[](const auto* left,const auto* right){return left->x<right->x;});
    NativeLineAdvance result{};
    auto advance=[&](float target) {
        const auto delta=std::round(target-result.pixels/scale);
        if(!std::isfinite(delta)||delta<-32768||delta>32767)
            throw std::length_error("Native paragraph advance exceeds capacity");
        const auto value=static_cast<int>(delta);result.pixels+=value*scale;return value;
    };
    for(const auto* object:objects) {
        result.icons.push_back({object->id,advance(object->x)});
        // Retain the native integer client-pixel rule at fractional scales.
        result.pixels+=std::round(paragraph.content().icons().at(object->id).advance*scale);
    }
    result.finish=advance(std::ceil(metrics.width));
    return result;
}
ParagraphTransport plan_paragraph_transport(const ShapedParagraph& paragraph,FontPageSize page,float scale) {
    if(page.width<=2||page.height<=2||page.width>32767||page.height>32767)
        throw std::invalid_argument("Invalid native paragraph page dimensions");
    if(!std::isfinite(scale)||scale<=0) throw std::invalid_argument("Invalid paragraph advance scale");
    ParagraphTransport result;
    std::string color;
    const auto& content=paragraph.content();
    auto select_color=[&](std::uint32_t id) {
        const auto& next=content.colors().at(id);
        result.text+=paragraph_color_transition(color,next);color=next;
    };
    auto append_glyph=[&](ScalarGlyph glyph) {
        if(result.glyphs.size()>=0xfffe) throw std::length_error("Native paragraph transport exceeds capacity");
        result.tokens.push_back(result.text.size());
        result.text+=encode(0xf0000+static_cast<std::uint32_t>(result.glyphs.size()));
        result.glyphs.push_back(std::move(glyph));
    };
    auto advance_glyph=[&](int advance) {
        ScalarGlyph blank{};blank.metrics.width=blank.metrics.height=1;blank.alpha={0};
        blank.metrics.advance=static_cast<std::int16_t>(advance);append_glyph(std::move(blank));
    };
    auto tiles=rasterize_paragraph(paragraph,page.width-2,page.height-2);
    for(std::size_t line=0;line<paragraph.lines().size();++line) {
        const auto& metrics=paragraph.lines()[line];
        const auto first=result.glyphs.size(),line_start=result.text.size();
        for(auto& tile:tiles) if(tile.line==line) {
            if(!std::isfinite(tile.x)||!std::isfinite(tile.y)||tile.x<-32768||tile.x>32767||
               tile.y-metrics.top<-32768||tile.y-metrics.top>32767)
                throw std::length_error("Native shaped glyph offset exceeds capacity");
            ScalarGlyph glyph{};
            glyph.metrics.width=static_cast<std::int16_t>(tile.bitmap.width);
            glyph.metrics.height=static_cast<std::int16_t>(tile.bitmap.height);
            glyph.metrics.x_offset=static_cast<std::int16_t>(tile.x);
            glyph.metrics.y_offset=static_cast<std::int16_t>(tile.y-metrics.top);
            glyph.alpha=std::move(tile.bitmap.alpha);select_color(tile.style);append_glyph(std::move(glyph));
        }
        const auto plan=plan_paragraph_line(paragraph,line,scale);
        for(const auto& icon:plan.icons) {
            const auto placement=std::find_if(paragraph.objects().begin(),paragraph.objects().end(),
                [&](const auto& object){return object.id==icon.id;});
            if(placement==paragraph.objects().end()) throw std::logic_error("Missing paragraph inline placement");
            select_color(placement->style);advance_glyph(icon.padding);
            result.text+=content.icons().at(icon.id).command;
        }
        if(!plan.icons.empty()||result.glyphs.size()==first) {select_color(0);advance_glyph(plan.finish);}
        else result.glyphs.back().metrics.advance=static_cast<std::int16_t>(plan.finish);
        if(line+1==paragraph.lines().size()) {result.text+=paragraph_color_transition(color,"");color.clear();}
        // Match the verified native 256-byte scratch word buffer.
        if(result.text.size()-line_start>240||std::ceil(metrics.width)>32767)
            throw std::length_error("Native shaped line exceeds transport capacity");
        if(line+1<paragraph.lines().size()) result.text+='\n';
    }
    if(result.text.size()>32000) throw std::length_error("Native paragraph transport exceeds capacity");
    return result;
}
std::uint32_t paragraph_token_start(const ParagraphTransport& plan,std::uint32_t first,std::uint32_t end) {
    if(first<0xf0000||end>0xffffe||first>end||plan.tokens.size()!=plan.glyphs.size())
        throw std::invalid_argument("Invalid paragraph token range");
    std::vector<std::uint32_t> literals;
    std::size_t token=0;
    for(std::size_t offset=0;offset<plan.text.size();) {
        const auto unit=decode(std::string_view(plan.text).substr(offset));
        if(token<plan.tokens.size()&&offset==plan.tokens[token]) {
            if(!unit.valid||unit.value!=0xf0000+token)
                throw std::invalid_argument("Invalid generated paragraph token");
            ++token;
        } else if(unit.valid&&unit.value>=0xf0000&&unit.value<end) {
            literals.push_back(unit.value);
        }
        offset+=unit.bytes;
    }
    if(token!=plan.tokens.size()) throw std::invalid_argument("Invalid paragraph token offsets");
    std::sort(literals.begin(),literals.end());
    for(const auto scalar:literals) {
        if(scalar<first) continue;
        if(scalar-first>=plan.glyphs.size()) break;
        first=scalar+1;
    }
    if(plan.glyphs.size()>end-first) throw std::length_error("Native paragraph transport token budget exhausted");
    return first;
}
void assign_paragraph_tokens(ParagraphTransport& plan,std::uint32_t first) {
    if(paragraph_token_start(plan,first)!=first)
        throw std::invalid_argument("Paragraph token range overlaps literal text");
    // Both ends are in plane 15, so replacement preserves byte positions.
    for(std::size_t index=0;index<plan.tokens.size();++index) {
        const auto assigned=encode(first+static_cast<std::uint32_t>(index));
        plan.text.replace(plan.tokens[index],assigned.size(),assigned);
    }
}
}
