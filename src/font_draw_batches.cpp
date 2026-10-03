#include "font_draw_batches.hpp"
#include <cmath>
#include <stdexcept>

namespace eu4unicode {
void tag_font_vertices(MapFontVertex* vertices,std::size_t count,std::uint32_t page) noexcept {
    if(!vertices||!page) return;
    const auto tag=static_cast<float>(page)*2;
    // Curved map labels reuse vertices produced by the plain-label path.
    // Replace their page tag rather than accumulating it on each pass.
    for(std::size_t i=0;i<count;++i) vertices[i].u=vertices[i].u-std::floor(vertices[i].u/2)*2+tag;
}
std::vector<FontDrawBatch> split_font_quads(std::vector<MapFontVertex>& vertices,std::size_t pages) {
    if(vertices.size()%4||!pages) throw std::invalid_argument("Invalid indexed font geometry");
    std::vector<FontDrawBatch> batches;
    for(std::size_t quad=0;quad<vertices.size()/4;++quad) {
        auto first=vertices.data()+quad*4;
        if(!std::isfinite(first->u)||first->u<0) throw std::invalid_argument("Invalid font page coordinate");
        const auto page_value=std::floor(first->u/2);
        if(page_value>=static_cast<float>(pages)) throw std::out_of_range("Font geometry refers to an unavailable page");
        const auto page=static_cast<std::uint32_t>(page_value);
        for(std::size_t i=0;i<4;++i) {
            const auto u=first[i].u-static_cast<float>(page)*2;
            if(!std::isfinite(u)||u<0||u>1||!std::isfinite(first[i].v)||first[i].v<0||first[i].v>1)
                throw std::invalid_argument("Font quad crosses texture pages");
        }
        if(!batches.empty()&&batches.back().page==page) ++batches.back().quad_count;
        else batches.push_back({page,quad,1});
    }
    // Validate the complete input before changing any coordinates.
    for(const auto& batch:batches)
        for(std::size_t i=batch.first_quad*4;i<(batch.first_quad+batch.quad_count)*4;++i)
            vertices[i].u-=static_cast<float>(batch.page)*2;
    return batches;
}
}
