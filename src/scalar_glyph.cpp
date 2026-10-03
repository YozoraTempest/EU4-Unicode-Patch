#include "scalar_glyph.hpp"
#include "unicode_text.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace eu4unicode {
ScalarGlyph rasterize_scalar(std::uint32_t scalar,int size,std::shared_ptr<const TextFonts> fonts) {
    if(size<=0||size>512) throw std::invalid_argument("Invalid native font size");
    TextLayout layout(encode(scalar),static_cast<float>(size),1024,1024,L"Microsoft YaHei UI",std::move(fonts));
    for(const auto& run:layout.glyph_runs())
        if(std::find(run.glyphs.begin(),run.glyphs.end(),0)!=run.glyphs.end())
            throw std::domain_error("Font collection has no glyph for Unicode scalar");
    const auto image=layout.rasterize();
    int left=static_cast<int>(image.width),top=static_cast<int>(image.height),right=-1,bottom=-1;
    for(std::uint32_t y=0;y<image.height;++y) for(std::uint32_t x=0;x<image.width;++x)
        if(image.pixels[(static_cast<std::size_t>(y)*image.width+x)*4+3]) {
            left=(std::min)(left,static_cast<int>(x)); top=(std::min)(top,static_cast<int>(y));
            right=(std::max)(right,static_cast<int>(x)); bottom=(std::max)(bottom,static_cast<int>(y));
        }
    ScalarGlyph result;
    result.metrics.advance=static_cast<std::int16_t>(std::ceil(layout.metrics().width));
    result.metrics.width=result.metrics.height=1;
    if(right<left) { result.alpha={0}; return result; }
    result.metrics.width=static_cast<std::int16_t>(right-left+1);
    result.metrics.height=static_cast<std::int16_t>(bottom-top+1);
    result.metrics.x_offset=static_cast<std::int16_t>(left-16);
    result.metrics.y_offset=static_cast<std::int16_t>(std::round(size*.8)-std::round(image.baseline)+top-16);
    result.alpha.resize(static_cast<std::size_t>(result.metrics.width)*result.metrics.height);
    for(int y=0;y<result.metrics.height;++y) for(int x=0;x<result.metrics.width;++x)
        result.alpha[static_cast<std::size_t>(y)*result.metrics.width+x]=
            image.pixels[(static_cast<std::size_t>(top+y)*image.width+left+x)*4+3];
    return result;
}
}
