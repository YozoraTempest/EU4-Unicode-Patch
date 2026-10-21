#include "unicode_layout.hpp"
#include "unicode_text.hpp"
#include "unicode_services.hpp"
#include <pango/pangocairo.h>
#include <pango/pangofc-fontmap.h>
#include <fontconfig/fcfreetype.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include "linux/render_api.hpp"

namespace eu4unicode {
namespace {
constexpr float unit=static_cast<float>(PANGO_SCALE);
template<class T> struct ObjectDelete { void operator()(T* p) const { if(p) g_object_unref(p); } };
template<class T> using Object=std::unique_ptr<T,ObjectDelete<T>>;
using Surface=std::unique_ptr<cairo_surface_t,decltype(&cairo_surface_destroy)>;
using Canvas=std::unique_ptr<cairo_t,decltype(&cairo_destroy)>;
using Description=std::unique_ptr<PangoFontDescription,decltype(&pango_font_description_free)>;
using Iterator=std::unique_ptr<PangoLayoutIter,decltype(&pango_layout_iter_free)>;
void checked(cairo_status_t status) {
    if(status!=CAIRO_STATUS_SUCCESS) throw std::runtime_error(cairo_status_to_string(status));
}
int units(float value) {
    if(!std::isfinite(value)||std::abs(value)>static_cast<double>(INT_MAX)/PANGO_SCALE)
        throw std::invalid_argument("Layout coordinate exceeds Pango range");
    return static_cast<int>(std::round(value*unit));
}
std::string utf8(std::wstring_view value) {
    std::string result;
    for(const auto cp:value) result+=encode(static_cast<std::uint32_t>(cp));
    return result;
}
void validate_range(std::string_view text,std::size_t start,std::size_t length) {
    if(start>text.size()||length>text.size()-start) throw std::invalid_argument("Layout range exceeds text");
    auto boundary=[&](std::size_t offset) { return offset==text.size()||(static_cast<unsigned char>(text[offset])&0xc0)!=0x80; };
    if(!boundary(start)||!boundary(start+length)) throw std::invalid_argument("Layout range splits a UTF-8 scalar");
}
void add_attribute(PangoAttrList* list,PangoAttribute* attr,std::size_t start,std::size_t length) {
    attr->start_index=static_cast<guint>(start);attr->end_index=static_cast<guint>(start+length);
    pango_attr_list_change(list,attr);
}
}
struct TextFonts::Impl {
    std::unique_ptr<FcConfig,decltype(&FcConfigDestroy)> config{nullptr,FcConfigDestroy};
    Object<PangoFontMap> map;
    std::vector<std::string> names;
    bool system_first;
    Impl(const std::vector<std::filesystem::path>& files,bool first):system_first(first) {
        if(files.empty()) throw std::invalid_argument("A private font collection needs font files");
        config.reset(FcInitLoadConfigAndFonts());
        if(!config) throw std::runtime_error("Fontconfig initialization failed");
        for(const auto& path:files) {
            if(!std::filesystem::is_regular_file(path)) throw std::invalid_argument("Font file is missing");
            const auto file=path.string();int count=0;
            std::unique_ptr<FcPattern,decltype(&FcPatternDestroy)> pattern(
                FcFreeTypeQuery(reinterpret_cast<const FcChar8*>(file.c_str()),0,nullptr,&count),FcPatternDestroy);
            if(!pattern||!FcConfigAppFontAddFile(config.get(),reinterpret_cast<const FcChar8*>(file.c_str())))
                throw std::invalid_argument("Fontconfig rejected application font");
            for(int index=0;index<count;++index) {
                if(index) pattern.reset(FcFreeTypeQuery(reinterpret_cast<const FcChar8*>(file.c_str()),index,nullptr,&count));
                FcChar8* family=nullptr;
                if(pattern&&FcPatternGetString(pattern.get(),FC_FAMILY,0,&family)==FcResultMatch) {
                    const std::string name(reinterpret_cast<const char*>(family));
                    if(std::find(names.begin(),names.end(),name)==names.end()) names.push_back(name);
                }
            }
        }
        if(names.empty()) throw std::invalid_argument("Private fonts have no usable families");
        map.reset(pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT));
        if(!map) throw std::runtime_error("Pango FreeType font map is unavailable");
        pango_fc_font_map_set_config(reinterpret_cast<PangoFcFontMap*>(map.get()),config.get());
    }
};
TextFonts::TextFonts(const std::vector<std::filesystem::path>& files,bool system_first):impl_(std::make_unique<Impl>(files,system_first)) {}
TextFonts::~TextFonts()=default;
std::vector<std::string> TextFonts::families() const { return impl_->names; }

class GlyphFace {
public:
    Object<PangoFont> font;
    cairo_scaled_font_t* scaled;
    float size;
    explicit GlyphFace(PangoFont* value,float em):font(static_cast<PangoFont*>(g_object_ref(value))),
        scaled(pango_cairo_font_get_scaled_font(reinterpret_cast<PangoCairoFont*>(value))),size(em) {
        if(!scaled) throw std::runtime_error("Pango did not retain a Cairo font");
    }
};
struct TextLayout::Impl {
    std::string text;
    std::vector<std::size_t> boundaries;
    std::shared_ptr<const TextFonts> fonts;
    Object<PangoFontMap> map;
    Object<PangoContext> context;
    Object<PangoLayout> layout;
    TextLayoutOptions options;
    float em_size;
    bool application_drawing=false;
    Impl(std::string_view value,float size,float width,float height,std::wstring_view family,
         std::shared_ptr<const TextFonts> custom,TextLayoutOptions settings):
        text(value),boundaries(grapheme_boundaries(value)),fonts(std::move(custom)),options(std::move(settings)),em_size(size) {
        if(!valid_utf8(value)||!std::isfinite(size)||!std::isfinite(width)||!std::isfinite(height)||size<=0||width<=0||height<=0||
           !std::isfinite(options.line_height)||options.line_height<0) throw std::invalid_argument("Invalid UTF-8 layout or dimensions");
        if(value.size()>INT_MAX) throw std::length_error("Text exceeds Pango analysis range");
        if(!fonts) map.reset(pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT));
        auto selected=fonts?fonts->impl_->map.get():map.get();
        if(!selected) throw std::runtime_error("Pango FreeType font map is unavailable");
        context.reset(pango_font_map_create_context(selected));
        pango_cairo_context_set_resolution(context.get(),72);
        std::unique_ptr<cairo_font_options_t,decltype(&cairo_font_options_destroy)> rendering(cairo_font_options_create(),cairo_font_options_destroy);
        cairo_font_options_set_antialias(rendering.get(),CAIRO_ANTIALIAS_GRAY);
        cairo_font_options_set_hint_style(rendering.get(),CAIRO_HINT_STYLE_NONE);
        cairo_font_options_set_hint_metrics(rendering.get(),CAIRO_HINT_METRICS_OFF);
        pango_cairo_context_set_font_options(context.get(),rendering.get());
        const auto direction=options.direction==TextDirection::Automatic?pango_find_base_dir(text.data(),static_cast<int>(text.size())):
            (options.direction==TextDirection::RightToLeft?PANGO_DIRECTION_RTL:PANGO_DIRECTION_LTR);
        pango_context_set_base_dir(context.get(),direction);
        layout.reset(pango_layout_new(context.get()));
        pango_layout_set_auto_dir(layout.get(),FALSE);
        pango_layout_set_alignment(layout.get(),PANGO_ALIGN_LEFT);
        pango_layout_set_wrap(layout.get(),PANGO_WRAP_WORD_CHAR);
        pango_layout_set_width(layout.get(),options.wrap?units(width):-1);
        pango_layout_set_text(layout.get(),text.data(),static_cast<int>(text.size()));
        Description description(pango_font_description_new(),pango_font_description_free);
        std::string families;
        auto append=[&](const std::string& name) { if(!families.empty()) families+=',';families+=name; };
        if(fonts&&!fonts->impl_->system_first) for(const auto& name:fonts->impl_->names) append(name);
        append(utf8(family));
        if(fonts&&fonts->impl_->system_first) for(const auto& name:fonts->impl_->names) append(name);
        pango_font_description_set_family(description.get(),families.c_str());
        pango_font_description_set_absolute_size(description.get(),units(size));
        pango_layout_set_font_description(layout.get(),description.get());
        std::unique_ptr<PangoAttrList,decltype(&pango_attr_list_unref)> attrs(pango_attr_list_new(),pango_attr_list_unref);
        // Fixed game line height is applied by our metrics, hit testing and
        // drawing below. Pango keeps its natural extents for shaping.
        for(const auto& style:options.styles) if(style.style) {
            validate_range(text,style.text_start,style.text_length);
            // Distinct paint attributes make Pango itemize at application style
            // boundaries. The native renderer owns the actual color/effect.
            add_attribute(attrs.get(),pango_attr_foreground_new((style.style&255)*257,((style.style>>8)&255)*257,
                ((style.style>>16)&255)*257),style.text_start,style.text_length);
            add_attribute(attrs.get(),pango_attr_foreground_alpha_new(((style.style>>24)&255)*257),style.text_start,style.text_length);
            application_drawing=true;
        }
        for(const auto& object:options.objects) {
            validate_range(text,object.text_start,object.text_length);
            if(!object.text_length||!std::isfinite(object.width)||!std::isfinite(object.height)||!std::isfinite(object.baseline)||
               object.width<0||object.height<=0||object.baseline<0||object.baseline>object.height)
                throw std::invalid_argument("Invalid inline object metrics");
            PangoRectangle rectangle{0,-units(object.baseline),units(object.width),units(object.height)};
            add_attribute(attrs.get(),pango_attr_shape_new(&rectangle,&rectangle),object.text_start,object.text_length);
            application_drawing=true;
        }
        pango_layout_set_attributes(layout.get(),attrs.get());
    }
    std::uint32_t style(std::size_t offset) const {
        std::uint32_t result=0;
        for(const auto& range:options.styles) if(offset>=range.text_start&&offset<range.text_start+range.text_length) result=range.style;
        return result;
    }
    int line_number(PangoLayoutLine* line) const {
        int index=0;
        for(auto list=pango_layout_get_lines_readonly(layout.get());list;list=list->next,++index)
            if(list->data==line) return index;
        throw std::logic_error("Pango run lost its owning line");
    }
    float run_baseline(PangoLayoutIter* iter) const {
        const auto native=pango_layout_iter_get_run_baseline(iter)/unit;
        if(options.line_height==0) return native;
        return native-pango_layout_iter_get_baseline(iter)/unit+
            line_number(pango_layout_iter_get_line_readonly(iter))*options.line_height+em_size*.8f;
    }
};
TextLayout::TextLayout(std::string_view text,float size,float width,float height,std::wstring_view family,
    std::shared_ptr<const TextFonts> fonts,TextLayoutOptions options):impl_(std::make_unique<Impl>(text,size,width,height,family,std::move(fonts),std::move(options))) {}
TextLayout::~TextLayout()=default;
TextLayout::TextLayout(TextLayout&&) noexcept=default;
TextLayout& TextLayout::operator=(TextLayout&&) noexcept=default;
LayoutMetrics TextLayout::metrics() const {
    PangoRectangle logical{};pango_layout_get_extents(impl_->layout.get(),nullptr,&logical);
    const auto count=static_cast<std::uint32_t>(pango_layout_get_line_count(impl_->layout.get()));
    return {logical.width/unit,impl_->options.line_height>0?count*impl_->options.line_height:logical.height/unit,count};
}
std::vector<LayoutLine> TextLayout::lines() const {
    std::vector<LayoutLine> result;
    Iterator iter(pango_layout_get_iter(impl_->layout.get()),pango_layout_iter_free);
    do {
        auto line=pango_layout_iter_get_line_readonly(iter.get());
        PangoRectangle logical{};pango_layout_iter_get_line_extents(iter.get(),nullptr,&logical);
        int top=0,bottom=0;pango_layout_iter_get_line_yrange(iter.get(),&top,&bottom);
        result.push_back({static_cast<std::size_t>(line->start_index),static_cast<std::size_t>(line->length),0,
            logical.width/unit,top/unit,(bottom-top)/unit,(pango_layout_iter_get_baseline(iter.get())-top)/unit});
        if(impl_->options.line_height>0) {
            auto& value=result.back();value.top=(result.size()-1)*impl_->options.line_height;
            value.height=impl_->options.line_height;value.baseline=impl_->em_size*.8f;
        }
    } while(pango_layout_iter_next_line(iter.get()));
    for(std::size_t index=0;index<result.size();++index) {
        auto& line=result[index];
        const auto next=index+1<result.size()?result[index+1].text_start:impl_->text.size();
        line.newline_length=next-line.text_start-line.text_length;line.text_length+=line.newline_length;
    }
    return result;
}
LayoutDrawing TextLayout::drawing() const {
    LayoutDrawing result;
    Iterator iter(pango_layout_get_iter(impl_->layout.get()),pango_layout_iter_free);
    do {
        const auto native=pango_layout_iter_get_run_readonly(iter.get());
        if(!native||!native->glyphs->num_glyphs) continue;
        const auto item=native->item;
        PangoRectangle logical{};pango_layout_iter_get_run_extents(iter.get(),nullptr,&logical);
        const auto baseline=impl_->run_baseline(iter.get());
        std::vector<const TextInlineObject*> objects;
        for(const auto& object:impl_->options.objects)
            if(object.text_start>=static_cast<std::size_t>(item->offset)&&
               object.text_start<static_cast<std::size_t>(item->offset+item->length)) objects.push_back(&object);
        if(!objects.empty()) {
            // Pango coalesces adjacent equal shape attributes into one item.
            // An item can therefore represent many distinct native bitmap
            // glyphs/icons, each with its own logical ID and visual position.
            std::sort(objects.begin(),objects.end(),[](const auto* left,const auto* right){return left->text_start<right->text_start;});
            std::size_t end=item->offset;
            for(const auto* object:objects) {
                if(object->text_start!=end) throw std::logic_error("Pango inline item has incomplete object coverage");
                PangoRectangle position{};
                pango_layout_index_to_pos(impl_->layout.get(),static_cast<int>(object->text_start),&position);
                const auto x=(position.x+std::min(position.width,0))/unit;
                result.objects.push_back({object->text_start,object->text_length,x,baseline-object->baseline,
                    object->width,object->height,object->id,impl_->style(object->text_start),(item->analysis.level&1)!=0});
                end=object->text_start+object->text_length;
            }
            if(end!=static_cast<std::size_t>(item->offset+item->length))
                throw std::logic_error("Pango inline item has trailing non-object text");
            continue;
        }
        if(!item->analysis.font) continue;
        Description description(pango_font_describe_with_absolute_size(item->analysis.font),pango_font_description_free);
        GlyphRun run;
        run.font_family=pango_font_description_get_family(description.get());
        run.em_size=pango_font_description_get_size(description.get())/unit;
        run.bidi_level=item->analysis.level;run.text_start=item->offset;run.text_length=item->length;
        const bool rtl=(run.bidi_level&1)!=0;
        run.baseline_x=(logical.x+(rtl?logical.width:0))/unit;run.baseline_y=baseline;
        run.face=std::make_shared<GlyphFace>(item->analysis.font,run.em_size);run.style=impl_->style(run.text_start);
        std::map<int,std::pair<std::uint32_t,std::uint32_t>> clusters;
        const auto count=native->glyphs->num_glyphs;
        for(int logical_index=0;logical_index<count;++logical_index) {
            const auto visual=rtl?count-logical_index-1:logical_index;
            const auto& glyph=native->glyphs->glyphs[visual];
            run.glyphs.push_back(glyph.glyph==PANGO_GLYPH_EMPTY?0xffff:
                (glyph.glyph&PANGO_GLYPH_UNKNOWN_FLAG)||glyph.glyph>0xffff?0:static_cast<std::uint16_t>(glyph.glyph));
            run.advances.push_back(glyph.geometry.width/unit);
            run.offsets.push_back({(rtl?-glyph.geometry.x_offset:glyph.geometry.x_offset)/unit,-glyph.geometry.y_offset/unit});
            auto [found,inserted]=clusters.try_emplace(native->glyphs->log_clusters[visual],logical_index,0);
            ++found->second.second;
        }
        for(auto it=clusters.begin();it!=clusters.end();++it) {
            const auto next=std::next(it);
            const auto end=next==clusters.end()?item->length:next->first;
            run.clusters.push_back({run.text_start+it->first,static_cast<std::size_t>(end-it->first),it->second.first,it->second.second});
        }
        result.runs.push_back(std::move(run));
    } while(pango_layout_iter_next_run(iter.get()));
    return result;
}
std::vector<GlyphRun> TextLayout::glyph_runs() const { return drawing().runs; }
HitPosition TextLayout::hit_test(float x,float y) const {
    const bool vertical_inside=y>=0&&y<metrics().height;
    if(impl_->options.line_height>0&&std::isfinite(y)) {
        const auto count=pango_layout_get_line_count(impl_->layout.get());
        const int line=std::clamp(static_cast<int>(std::floor(y/impl_->options.line_height)),0,count-1);
        Iterator iter(pango_layout_get_iter(impl_->layout.get()),pango_layout_iter_free);
        for(int index=0;index<line;++index) pango_layout_iter_next_line(iter.get());
        int top=0,bottom=0;pango_layout_iter_get_line_yrange(iter.get(),&top,&bottom);
        y=(top+bottom)/(2*unit);
    }
    int index=0,trailing=0;
    const auto inside=pango_layout_xy_to_index(impl_->layout.get(),units(x),units(y),&index,&trailing);
    std::size_t offset=std::clamp<std::size_t>(index,0,impl_->text.size());
    for(int n=0;n<trailing&&offset<impl_->text.size();++n) offset+=decode(std::string_view(impl_->text).substr(offset)).bytes;
    auto boundary=std::lower_bound(impl_->boundaries.begin(),impl_->boundaries.end(),offset);
    if(boundary==impl_->boundaries.end()) offset=impl_->text.size();
    else if(*boundary!=offset) offset=trailing?*boundary:*(boundary-1);
    return {offset,inside!=FALSE&&vertical_inside,trailing!=0};
}
CaretPosition TextLayout::caret(std::size_t offset,bool trailing) const {
    const auto& boundaries=impl_->boundaries;
    const auto found=std::lower_bound(boundaries.begin(),boundaries.end(),offset);
    if(found==boundaries.end()||*found!=offset) throw std::invalid_argument("Caret position must be a UTF-8 grapheme boundary");
    auto index=offset;
    if(trailing&&found!=boundaries.begin()) index=*std::prev(found);else trailing=false;
    PangoRectangle position{};pango_layout_index_to_pos(impl_->layout.get(),static_cast<int>(index),&position);
    if(impl_->options.line_height>0) {
        int line=0,x=0;pango_layout_index_to_line_x(impl_->layout.get(),static_cast<int>(index),FALSE,&line,&x);
        position.y=units(line*impl_->options.line_height);position.height=units(impl_->options.line_height);
    }
    return {offset,(position.x+(trailing?position.width:0))/unit,position.y/unit,position.height/unit,trailing};
}
std::vector<SelectionRegion> TextLayout::selection(std::size_t begin,std::size_t end) const {
    if(begin>end||end>impl_->text.size()) throw std::out_of_range("Selection exceeds UTF-8 text");
    if(begin==end) return {};
    if(!std::binary_search(impl_->boundaries.begin(),impl_->boundaries.end(),begin)||
       !std::binary_search(impl_->boundaries.begin(),impl_->boundaries.end(),end))
        throw std::invalid_argument("Selection must contain complete graphemes");
    std::vector<SelectionRegion> result;
    Iterator iter(pango_layout_get_iter(impl_->layout.get()),pango_layout_iter_free);
    do {
        const auto native=pango_layout_iter_get_run_readonly(iter.get());
        if(!native) continue;
        const auto item=native->item;
        const auto first=std::max(begin,static_cast<std::size_t>(item->offset));
        const auto last=std::min(end,static_cast<std::size_t>(item->offset+item->length));
        if(first>=last) continue;
        int *ranges=nullptr,count=0,top=0,bottom=0;
        pango_layout_line_get_x_ranges(pango_layout_iter_get_line_readonly(iter.get()),first,last,&ranges,&count);
        std::unique_ptr<int,decltype(&g_free)> owned(ranges,g_free);
        pango_layout_iter_get_line_yrange(iter.get(),&top,&bottom);
        if(impl_->options.line_height>0) {
            top=units(impl_->line_number(pango_layout_iter_get_line_readonly(iter.get()))*impl_->options.line_height);
            bottom=top+units(impl_->options.line_height);
        }
        for(int index=0;index<count;++index) if(ranges[index*2+1]>ranges[index*2])
            result.push_back({first,last-first,ranges[index*2]/unit,top/unit,(ranges[index*2+1]-ranges[index*2])/unit,
                (bottom-top)/unit,item->analysis.level});
    } while(pango_layout_iter_next_run(iter.get()));
    return result;
}
GlyphBitmap rasterize_glyph_run(const GlyphRun& run) {
    if(!run.face||!std::isfinite(run.em_size)||run.em_size<=0||!std::isfinite(run.baseline_x)||!std::isfinite(run.baseline_y)||
       run.glyphs.size()>INT_MAX||run.offsets.size()!=run.glyphs.size()||run.advances.size()!=run.glyphs.size())
        throw std::invalid_argument("Incomplete shaped glyph run");
    if(run.em_size>16384) throw std::length_error("Shaped glyph size exceeds raster budget");
    std::vector<cairo_glyph_t> glyphs;
    double pen=run.baseline_x-std::floor(run.baseline_x);
    const double phase=run.baseline_y-std::floor(run.baseline_y);
    const bool rtl=(run.bidi_level&1)!=0;
    for(std::size_t index=0;index<run.glyphs.size();++index) {
        const auto offset=run.offsets[index];const auto advance=run.advances[index];
        if(!std::isfinite(advance)||!std::isfinite(offset.advance)||!std::isfinite(offset.ascender))
            throw std::invalid_argument("Glyph geometry must be finite");
        if(rtl) pen-=advance;
        if(run.glyphs[index]!=0xffff) glyphs.push_back({run.glyphs[index],pen+(rtl?-offset.advance:offset.advance),phase-offset.ascender});
        if(!rtl) pen+=advance;
    }
    if(glyphs.empty()) return {};
    Surface measure(cairo_recording_surface_create(CAIRO_CONTENT_ALPHA,nullptr),cairo_surface_destroy);
    Canvas canvas(cairo_create(measure.get()),cairo_destroy);
    auto font=run.face->scaled;
    std::unique_ptr<cairo_scaled_font_t,decltype(&cairo_scaled_font_destroy)> resized(nullptr,cairo_scaled_font_destroy);
    if(run.em_size!=run.face->size) {
        cairo_matrix_t matrix,ctm;cairo_scaled_font_get_font_matrix(font,&matrix);cairo_scaled_font_get_ctm(font,&ctm);
        cairo_matrix_scale(&matrix,run.em_size/run.face->size,run.em_size/run.face->size);
        std::unique_ptr<cairo_font_options_t,decltype(&cairo_font_options_destroy)> options(cairo_font_options_create(),cairo_font_options_destroy);
        cairo_scaled_font_get_font_options(font,options.get());
        resized.reset(cairo_scaled_font_create(cairo_scaled_font_get_font_face(font),&matrix,&ctm,options.get()));font=resized.get();
    }
    checked(cairo_scaled_font_status(font));cairo_set_scaled_font(canvas.get(),font);
    cairo_set_source_rgba(canvas.get(),1,1,1,1);cairo_show_glyphs(canvas.get(),glyphs.data(),glyphs.size());
    checked(cairo_status(canvas.get()));
    double x=0,y=0,ink_width=0,ink_height=0;
    cairo_recording_surface_ink_extents(measure.get(),&x,&y,&ink_width,&ink_height);
    if(ink_width==0||ink_height==0) return {};
    // Recording extents use glyph metrics; Cairo's subpixel glyph mask can
    // reach one pixel beyond them. A tight target clips that fringe in the
    // image compositor. Render with a guard, then retain only actual ink.
    const auto left=std::floor(x)-1,top=std::floor(y)-1;
    const auto width=std::ceil(x+ink_width)-left+1,height=std::ceil(y+ink_height)-top+1;
    if(!std::isfinite(width)||!std::isfinite(height)||width<0||height<0||width>16384||height>16384||width*height>64*1024*1024||
       std::abs(left)>INT_MAX||std::abs(top)>INT_MAX) throw std::length_error("Shaped glyph bitmap exceeds raster budget");
    GlyphBitmap result{static_cast<std::uint32_t>(width),static_cast<std::uint32_t>(height),static_cast<int>(left),static_cast<int>(top),{}};
    Surface surface(cairo_image_surface_create(CAIRO_FORMAT_A8,result.width,result.height),cairo_surface_destroy);
    checked(cairo_surface_status(surface.get()));
    Canvas raster(cairo_create(surface.get()),cairo_destroy);
    cairo_set_scaled_font(raster.get(),font);cairo_set_source_rgba(raster.get(),1,1,1,1);
    for(auto& glyph:glyphs) { glyph.x-=left;glyph.y-=top; }
    cairo_show_glyphs(raster.get(),glyphs.data(),glyphs.size());checked(cairo_status(raster.get()));cairo_surface_flush(surface.get());
    const auto stride=cairo_image_surface_get_stride(surface.get());const auto data=cairo_image_surface_get_data(surface.get());
    std::uint32_t first_x=result.width,first_y=result.height,last_x=0,last_y=0;
    for(std::uint32_t row=0;row<result.height;++row) for(std::uint32_t column=0;column<result.width;++column) {
        if(!data[row*stride+column]) continue;
        first_x=std::min(first_x,column);first_y=std::min(first_y,row);
        last_x=std::max(last_x,column+1);last_y=std::max(last_y,row+1);
    }
    if(first_x==result.width) return {};
    if(std::abs(left+first_x)>INT_MAX||std::abs(top+first_y)>INT_MAX)
        throw std::length_error("Shaped glyph ink offset exceeds raster capacity");
    result.left=static_cast<int>(left+first_x);result.top=static_cast<int>(top+first_y);
    result.width=last_x-first_x;result.height=last_y-first_y;
    result.alpha.resize(static_cast<std::size_t>(result.width)*result.height);
    for(std::uint32_t row=0;row<result.height;++row)
        std::memcpy(result.alpha.data()+row*result.width,data+(first_y+row)*stride+first_x,result.width);
    return result;
}
RasterImage TextLayout::rasterize() const {
    if(impl_->application_drawing) throw std::logic_error("Application text effects require the custom drawing renderer");
    const auto size=metrics();
    if(size.width>16320||size.height>16320||(size.width+32)*(size.height+32)>16*1024*1024)
        throw std::length_error("Layout bitmap exceeds raster budget");
    RasterImage result{static_cast<std::uint32_t>(std::ceil(std::max(1.f,size.width)))+32,
        static_cast<std::uint32_t>(std::ceil(std::max(1.f,size.height)))+32,pango_layout_get_baseline(impl_->layout.get())/unit,{}};
    Surface surface(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,result.width,result.height),cairo_surface_destroy);
    Canvas canvas(cairo_create(surface.get()),cairo_destroy);
    cairo_translate(canvas.get(),16,16);cairo_set_source_rgba(canvas.get(),1,1,1,1);
    if(impl_->options.line_height==0) pango_cairo_show_layout(canvas.get(),impl_->layout.get());
    else {
        int index=0;
        for(auto list=pango_layout_get_lines_readonly(impl_->layout.get());list;list=list->next,++index) {
            cairo_move_to(canvas.get(),0,index*impl_->options.line_height+impl_->em_size*.8f);
            pango_cairo_show_layout_line(canvas.get(),static_cast<PangoLayoutLine*>(list->data));
        }
        result.baseline=impl_->em_size*.8f;
    }
    checked(cairo_status(canvas.get()));cairo_surface_flush(surface.get());
    result.pixels.resize(static_cast<std::size_t>(result.width)*result.height*4);
    const auto data=cairo_image_surface_get_data(surface.get());const auto stride=cairo_image_surface_get_stride(surface.get());
    for(std::size_t row=0;row<result.height;++row) std::memcpy(result.pixels.data()+row*result.width*4,data+row*stride,result.width*4);
    return result;
}
void TextLayout::render_png(const std::filesystem::path& path) const {
    auto image=rasterize();
    // Diagnostics use black ink on white while atlas pixels remain white ink
    // on transparent, matching the renderer's multiplication convention.
    for(std::size_t index=0;index<image.pixels.size();index+=4) {
        const auto alpha=image.pixels[index+3];
        image.pixels[index]=image.pixels[index+1]=image.pixels[index+2]=255-alpha;image.pixels[index+3]=255;
    }
    Surface surface(cairo_image_surface_create_for_data(image.pixels.data(),CAIRO_FORMAT_ARGB32,image.width,image.height,image.width*4),cairo_surface_destroy);
    checked(cairo_surface_write_to_png(surface.get(),path.c_str()));
}
}
