#include "font_draw_batches.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace eu4unicode {
namespace {
template<class Vertex>
void tag_vertices(Vertex* vertices,std::size_t count,std::uint32_t page) noexcept {
    if(!vertices||!page) return;
    const auto tag=static_cast<float>(page)*2;
    // Curved map labels reuse vertices produced by the plain-label path.
    // Replace their page tag rather than accumulating it on each pass.
    for(std::size_t i=0;i<count;++i) vertices[i].u=vertices[i].u-std::floor(vertices[i].u/2)*2+tag;
}
template<class Vertex>
std::vector<FontDrawBatch> split_glyphs(std::vector<Vertex>& vertices,const std::vector<FontPageSize>& pages,std::size_t unit) {
    if(vertices.size()%unit||pages.empty()) throw std::invalid_argument("Invalid font geometry");
    for(const auto& size:pages)
        if(!size.width||!size.height||size.width>pages.front().width||size.height>pages.front().height)
            throw std::invalid_argument("Invalid font page dimensions");
    std::vector<FontDrawBatch> batches;
    for(std::size_t quad=0;quad<vertices.size()/unit;++quad) {
        auto first=vertices.data()+quad*unit;
        if(!std::isfinite(first->u)||first->u<0) throw std::invalid_argument("Invalid font page coordinate");
        const auto page_value=std::floor(first->u/2);
        if(page_value>=static_cast<float>(pages.size())) throw std::out_of_range("Font geometry refers to an unavailable page");
        const auto page=static_cast<std::uint32_t>(page_value);
        const auto scale_u=static_cast<float>(pages.front().width)/pages[page].width;
        const auto scale_v=static_cast<float>(pages.front().height)/pages[page].height;
        for(std::size_t i=0;i<unit;++i) {
            const auto u=first[i].u-static_cast<float>(page)*2;
            if(!std::isfinite(u)||u<0||u>1||!std::isfinite(first[i].v)||first[i].v<0||first[i].v>1||
               u*scale_u>1.00001f||first[i].v*scale_v>1.00001f)
                throw std::invalid_argument("Font quad crosses texture pages");
        }
        if(!batches.empty()&&batches.back().page==page) ++batches.back().quad_count;
        else batches.push_back({page,quad,1});
    }
    // Validate the complete input before changing any coordinates.
    for(const auto& batch:batches) {
        const auto scale_u=static_cast<float>(pages.front().width)/pages[batch.page].width;
        const auto scale_v=static_cast<float>(pages.front().height)/pages[batch.page].height;
        for(std::size_t i=batch.first_quad*unit;i<(batch.first_quad+batch.quad_count)*unit;++i) {
            vertices[i].u=(vertices[i].u-static_cast<float>(batch.page)*2)*scale_u;
            vertices[i].v*=scale_v;
        }
    }
    return batches;
}
}
FontPageSize supplemental_font_page_size(FontPageSize original) {
    if(original.width<=2||original.height<=2||original.width>16384||original.height>16384)
        throw std::invalid_argument("Invalid original font dimensions");
    return {(std::min)(original.width,2048u),(std::min)(original.height,2048u)};
}
void check_font_page_budget(std::uint64_t supplemental_bytes,std::uint64_t initial_staging_bytes,FontPageSize next) {
    constexpr std::uint64_t budget=256ull*1024*1024;
    if(next.width<=2||next.height<=2||next.width>2048||next.height>2048)
        throw std::invalid_argument("Invalid supplemental font dimensions");
    const auto bytes=next.rgba_bytes();
    if(!bytes||bytes>budget||supplemental_bytes>budget-bytes||
       initial_staging_bytes>budget-bytes-supplemental_bytes)
        throw std::length_error("Native font texture memory budget exhausted");
}
void tag_font_vertices(MapFontVertex* vertices,std::size_t count,std::uint32_t page) noexcept { tag_vertices(vertices,count,page); }
void tag_font_vertices(PopupFontVertex* vertices,std::size_t count,std::uint32_t page) noexcept { tag_vertices(vertices,count,page); }
std::vector<FontDrawBatch> split_font_quads(std::vector<MapFontVertex>& vertices,const std::vector<FontPageSize>& pages) { return split_glyphs(vertices,pages,4); }
std::vector<FontDrawBatch> split_popup_glyphs(std::vector<PopupFontVertex>& vertices,const std::vector<FontPageSize>& pages) { return split_glyphs(vertices,pages,6); }
}
