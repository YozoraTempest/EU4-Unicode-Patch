#include "unicode_layout.hpp"
#include <pango/pangocairo.h>
#include <memory>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
constexpr unsigned width=512,height=128,margin=16;
template<class T> using Object=std::unique_ptr<T,decltype(&g_object_unref)>;
void check(bool value,const char* message) {
    if(!value) {std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
std::vector<unsigned char> reference_ink(const char* text,float size) {
    Object<PangoFontMap> map(pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT),g_object_unref);
    check(map!=nullptr,"Pango FreeType map exists");
    Object<PangoContext> context(pango_font_map_create_context(map.get()),g_object_unref);
    pango_cairo_context_set_resolution(context.get(),72);
    std::unique_ptr<cairo_font_options_t,decltype(&cairo_font_options_destroy)> options(cairo_font_options_create(),cairo_font_options_destroy);
    cairo_font_options_set_antialias(options.get(),CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(options.get(),CAIRO_HINT_STYLE_NONE);
    cairo_font_options_set_hint_metrics(options.get(),CAIRO_HINT_METRICS_OFF);
    pango_cairo_context_set_font_options(context.get(),options.get());
    pango_context_set_base_dir(context.get(),pango_find_base_dir(text,static_cast<int>(std::strlen(text))));
    Object<PangoLayout> layout(pango_layout_new(context.get()),g_object_unref);
    pango_layout_set_auto_dir(layout.get(),FALSE);pango_layout_set_alignment(layout.get(),PANGO_ALIGN_LEFT);
    pango_layout_set_text(layout.get(),text,-1);
    std::unique_ptr<PangoFontDescription,decltype(&pango_font_description_free)> description(pango_font_description_new(),pango_font_description_free);
    pango_font_description_set_family(description.get(),"DejaVu Sans");
    pango_font_description_set_absolute_size(description.get(),std::round(size*PANGO_SCALE));
    pango_layout_set_font_description(layout.get(),description.get());
    // Use the production A8 format: ARGB32 can take a different Cairo glyph
    // compositor path and is not a pixel-exact reference for an alpha atlas.
    std::unique_ptr<cairo_surface_t,decltype(&cairo_surface_destroy)> surface(
        cairo_image_surface_create(CAIRO_FORMAT_A8,width,height),cairo_surface_destroy);
    std::unique_ptr<cairo_t,decltype(&cairo_destroy)> canvas(cairo_create(surface.get()),cairo_destroy);
    cairo_move_to(canvas.get(),margin,margin+size*.8f);cairo_set_source_rgba(canvas.get(),1,1,1,1);
    pango_cairo_show_layout_line(canvas.get(),pango_layout_get_line_readonly(layout.get(),0));
    check(cairo_status(canvas.get())==CAIRO_STATUS_SUCCESS,"native Pango reference renders successfully");
    cairo_surface_flush(surface.get());
    std::vector<unsigned char> result(width*height);
    const auto* data=cairo_image_surface_get_data(surface.get());const auto stride=cairo_image_surface_get_stride(surface.get());
    for(unsigned row=0;row<height;++row) std::copy_n(data+row*stride,width,result.data()+row*width);
    return result;
}
}
int main() {
    using namespace eu4unicode;
    unsigned cases=0;
    // 24pt catches the bottom fringe of resh/bet. The fractional sizes also
    // exercise Cairo's four subpixel phases at the 0.8em native baseline.
    for(const float size:{16.f,23.75f,24.f,24.0625f,24.375f,24.6875f,31.f}) {
        for(const auto* text:{u8"עברית",u8"ר",u8"ב",u8"ת",u8"AVagj",u8"a\u0323\u0301"}) {
            TextLayoutOptions options{TextDirection::Automatic,false,size};
            TextLayout layout(text,size,400,100,L"DejaVu Sans",{},options);
            // Pango draws the source text directly; it never uses our glyph
            // extraction, recording extents or cropped bitmap implementation.
            const auto direct=reference_ink(text,size);
            std::vector<unsigned char> alpha(width*height);
            for(const auto& run:layout.glyph_runs()) {
                check(run.font_family=="DejaVu Sans","install DejaVu Sans for the glyph clipping regression");
                const auto raster=rasterize_glyph_run(run);
                for(unsigned row=0;row<raster.height;++row) for(unsigned column=0;column<raster.width;++column) {
                    const int x=static_cast<int>(std::floor(run.baseline_x))+raster.left+static_cast<int>(column)+static_cast<int>(margin);
                    const int y=static_cast<int>(std::floor(run.baseline_y))+raster.top+static_cast<int>(row)+static_cast<int>(margin);
                    check(x>=0&&y>=0&&x<static_cast<int>(width)&&y<static_cast<int>(height),
                        "glyph ink stays within the reference surface");
                    auto& target=alpha[y*width+x];
                    const auto source=raster.alpha[row*raster.width+column];
                    target=static_cast<unsigned char>(source+(target*(255-source)+127)/255);
                }
            }
            std::size_t changed=0;unsigned difference=0;
            for(std::size_t i=0;i<alpha.size();++i) {
                const auto delta=std::abs(int(alpha[i])-direct[i]);
                changed+=delta!=0;difference+=delta;
            }
            if(changed) std::cerr<<"size="<<size<<" text="<<text<<" changed="<<changed<<" difference="<<difference<<'\n';
            check(!changed,"glyph raster retains the exact Pango ink at fractional baselines");
            ++cases;
        }
    }
    std::cout<<cases<<" exact Pango/glyph raster comparisons passed\n";
}
