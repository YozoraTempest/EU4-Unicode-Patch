#include "paragraph_transport.hpp"
#include "formatted_paragraph.hpp"
#include "formatted_text.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace eu4unicode {
std::vector<ScalarGlyph> map_cluster_glyphs(const ShapedParagraph& paragraph,FontPageSize page) {
    if(page.width<=2||page.height<=2||page.width>32767||page.height>32767)
        throw std::invalid_argument("Invalid native map page dimensions");
    if(paragraph.lines().size()!=1||!paragraph.objects().empty()) throw std::invalid_argument("Map labels require one plain line");
    struct Cluster { GlyphBitmap bitmap;float pen,x,y; };
    std::vector<Cluster> clusters;
    std::size_t bytes=0;
    for(const auto& run:paragraph.runs()) {
        std::vector<float> positions(run.advances.size()+1);
        for(std::size_t index=0;index<run.advances.size();++index) positions[index+1]=positions[index]+run.advances[index];
        const bool rtl=(run.bidi_level&1)!=0;
        for(const auto& cluster:run.clusters) {
            const auto first=cluster.first_glyph,last=first+cluster.glyph_count;
            GlyphRun slice{};
            slice.font_family=run.font_family;slice.bidi_level=run.bidi_level;slice.em_size=run.em_size;
            slice.baseline_y=run.baseline_y;slice.sideways=run.sideways;slice.face=run.face;
            slice.measuring=run.measuring;slice.style=run.style;
            slice.text_start=cluster.text_start;slice.text_length=cluster.text_length;
            slice.glyphs.assign(run.glyphs.begin()+first,run.glyphs.begin()+last);
            slice.advances.assign(run.advances.begin()+first,run.advances.begin()+last);
            slice.offsets.assign(run.offsets.begin()+first,run.offsets.begin()+last);
            slice.baseline_x=run.baseline_x+(rtl?-positions[first]:positions[first]);
            auto bitmap=rasterize_glyph_run(slice);
            if(bitmap.width>static_cast<unsigned>(page.width-2)||bitmap.height>static_cast<unsigned>(page.height-2))
                throw std::length_error("Map cluster exceeds atlas page dimensions");
            bytes+=bitmap.alpha.size();if(bytes>16ull*1024*1024) throw std::length_error("Map label raster exceeds CPU budget");
            const auto x=std::floor(slice.baseline_x)+bitmap.left,y=std::floor(slice.baseline_y)+bitmap.top;
            const auto pen=run.baseline_x+(rtl?-positions[last]:positions[first]);
            if(!bitmap.width||!bitmap.height) { bitmap.width=bitmap.height=1;bitmap.alpha={0}; }
            clusters.push_back({std::move(bitmap),pen,x,y});
        }
    }
    std::stable_sort(clusters.begin(),clusters.end(),[](const auto& left,const auto& right){return left.pen<right.pen;});
    std::vector<ScalarGlyph> result;result.reserve(clusters.size());
    int pen=0;
    for(std::size_t index=0;index<clusters.size();++index) {
        auto& cluster=clusters[index];
        const auto next=index+1<clusters.size()?std::round(clusters[index+1].pen):std::ceil(paragraph.metrics().width);
        const auto advance=next-pen,x=std::round(cluster.x)-pen,y=std::round(cluster.y);
        if(advance<-32768||advance>32767||x<-32768||x>32767||y<-32768||y>32767)
            throw std::length_error("Map cluster geometry exceeds native capacity");
        ScalarGlyph glyph{};glyph.metrics.width=static_cast<std::int16_t>(cluster.bitmap.width);
        glyph.metrics.height=static_cast<std::int16_t>(cluster.bitmap.height);
        glyph.metrics.x_offset=static_cast<std::int16_t>(x);glyph.metrics.y_offset=static_cast<std::int16_t>(y);
        glyph.metrics.advance=static_cast<std::int16_t>(advance);glyph.alpha=std::move(cluster.bitmap.alpha);
        result.push_back(std::move(glyph));pen+=static_cast<int>(advance);
    }
    return result;
}

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
ParagraphTransport plan_paragraph_transport(const ShapedParagraph& paragraph,FontPageSize page,float scale,const ParagraphViewport* viewport) {
    if(page.width<=2||page.height<=2||page.width>32767||page.height>32767)
        throw std::invalid_argument("Invalid native paragraph page dimensions");
    if(!std::isfinite(scale)||scale<=0) throw std::invalid_argument("Invalid paragraph advance scale");
    if(viewport&&(!std::isfinite(viewport->left)||!std::isfinite(viewport->width)||viewport->left<0||
        viewport->width<=0||viewport->width>32767||paragraph.lines().size()!=1))
        throw std::invalid_argument("Invalid single-line paragraph viewport");
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
        const auto first=result.glyphs.size();
        for(auto& tile:tiles) if(tile.line==line) {
            if(viewport) {
                const auto begin=static_cast<unsigned>(std::clamp(std::ceil(viewport->left-tile.x),0.f,static_cast<float>(tile.bitmap.width)));
                const auto end=static_cast<unsigned>(std::clamp(std::ceil(viewport->left+viewport->width-tile.x),0.f,static_cast<float>(tile.bitmap.width)));
                if(begin>=end) continue;
                const auto old_width=tile.bitmap.width;
                std::vector<std::uint8_t> clipped(static_cast<std::size_t>(end-begin)*tile.bitmap.height);
                for(unsigned row=0;row<tile.bitmap.height;++row)
                    std::copy_n(tile.bitmap.alpha.data()+static_cast<std::size_t>(row)*old_width+begin,end-begin,
                        clipped.data()+static_cast<std::size_t>(row)*(end-begin));
                tile.bitmap.width=end-begin;tile.bitmap.alpha=std::move(clipped);
                tile.x=std::round(tile.x+begin-viewport->left);
            }
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
        auto plan=plan_paragraph_line(paragraph,line,scale);
        if(viewport) {
            plan={};
            std::vector<const InlinePlacement*> objects;
            for(const auto& object:paragraph.objects()) if(object.x+object.width>viewport->left&&object.x<viewport->left+viewport->width)
                objects.push_back(&object);
            std::stable_sort(objects.begin(),objects.end(),[](const auto* a,const auto* b){return a->x<b->x;});
            for(const auto* object:objects) {
                const auto padding=static_cast<int>(std::round(object->x-viewport->left-plan.pixels/scale));
                plan.icons.push_back({object->id,padding});
                plan.pixels+=padding*scale+std::round(paragraph.content().icons().at(object->id).advance*scale);
            }
            plan.finish=static_cast<int>(std::round(viewport->width-plan.pixels/scale));
        }
        for(const auto& icon:plan.icons) {
            const auto placement=std::find_if(paragraph.objects().begin(),paragraph.objects().end(),
                [&](const auto& object){return object.id==icon.id;});
            if(placement==paragraph.objects().end()) throw std::logic_error("Missing paragraph inline placement");
            select_color(placement->style);
            if(icon.padding) advance_glyph(icon.padding);
            result.text+=content.icons().at(icon.id).command;
        }
        if(!plan.icons.empty()||result.glyphs.size()==first) {
            if(plan.finish||result.glyphs.size()==first) {select_color(0);advance_glyph(plan.finish);}
        }
        else result.glyphs.back().metrics.advance=static_cast<std::int16_t>(plan.finish);
        if(line+1==paragraph.lines().size()) {result.text+=paragraph_color_transition(color,"");color.clear();}
        if(std::ceil(metrics.width)>32767)
            throw std::length_error("Native shaped line exceeds transport capacity");
        if(line+1<paragraph.lines().size()) result.text+='\n';
    }
    // A zero-height native glyph submits the current scratch word without
    // advancing the pen or adding a line. Break only at whole native commands;
    // never let the game's 256-byte word buffer split a token or UTF-8 scalar.
    ParagraphTransport bounded;
    std::size_t token=0,chunk=0;
    auto append=[&](ScalarGlyph glyph) {
        if(bounded.glyphs.size()>=0xfffe) throw std::length_error("Native paragraph transport exceeds capacity");
        bounded.tokens.push_back(bounded.text.size());
        bounded.text+=encode(0xf0000+static_cast<std::uint32_t>(bounded.glyphs.size()));
        bounded.glyphs.push_back(std::move(glyph));
    };
    for(std::size_t offset=0;offset<result.text.size();) {
        const auto unit=native_text_unit(result.text,offset,content.formatted());
        const auto bytes=unit.end-offset;
        if(chunk+bytes>240) {append(ScalarGlyph{});chunk=0;}
        if(token<result.tokens.size()&&result.tokens[token]==offset) append(std::move(result.glyphs[token++]));
        else bounded.text.append(result.text,offset,bytes);
        chunk=unit.kind==TextUnitKind::glyph&&unit.scalar=='\n'?0:chunk+bytes;
        offset=unit.end;
    }
    if(bounded.text.size()>32000) throw std::length_error("Native paragraph transport exceeds capacity");
    return bounded;
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
