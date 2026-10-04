#include "font_draw_batches.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
std::vector<eu4unicode::MapFontVertex> quads(std::initializer_list<unsigned> pages) {
    std::vector<eu4unicode::MapFontVertex> result;
    for(const auto page:pages) {
        const auto first=result.size();
        for(unsigned i=0;i<4;++i) result.push_back({static_cast<float>(first+i),2,3,i%2?0.875f:0.125f,i/2?0.75f:0.25f});
        eu4unicode::tag_font_vertices(result.data()+first,4,page);
    }
    return result;
}
}
int main() {
    try {
        std::vector<eu4unicode::MapFontVertex> native{{1,2,3,2.75f,0.5f}};
        eu4unicode::tag_font_vertices(native.data(),1,0);
        require(native[0].u==2.75f,"An unpaged native/mod font had its UV changed");
        auto vertices=quads({0,1,1,0,2,1});
        eu4unicode::tag_font_vertices(vertices.data()+4,4,1);
        require(vertices[4].u==2.125f,"Curved label processing accumulated the page tag");
        const auto before=vertices;
        const auto batches=eu4unicode::split_font_quads(vertices,3);
        require(batches.size()==5&&batches[1].page==1&&batches[1].first_quad==1&&batches[1].quad_count==2,
                "Font draw grouping reordered or merged nonadjacent glyphs");
        for(std::size_t i=0;i<vertices.size();++i) {
            require(vertices[i].x==before[i].x&&vertices[i].y==before[i].y&&vertices[i].z==before[i].z&&vertices[i].v==before[i].v,
                    "Page decoding changed native positions or vertical UVs");
            require(vertices[i].u==(i%2?0.875f:0.125f),"Page decoding changed the physical UV");
        }
        auto missing=quads({0,2});bool rejected=false;
        try { eu4unicode::split_font_quads(missing,2); } catch(const std::out_of_range&) { rejected=true; }
        require(rejected&&missing[4].u==4.125f,"Unavailable page changed cached geometry");
        auto mixed=quads({1});mixed[3].u=0.5f;rejected=false;
        try { eu4unicode::split_font_quads(mixed,2); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected&&mixed[0].u==2.125f,"A quad spanning pages was accepted or partially decoded");
        auto invalid=quads({0});invalid[0].u=std::nanf("");rejected=false;
        try { eu4unicode::split_font_quads(invalid,1); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected,"Non-finite page coordinate was accepted");
        std::vector<eu4unicode::PopupFontVertex> popup;
        for(const auto page:{0u,1u,1u,0u,2u}) {
            const auto first=popup.size();
            for(unsigned i=0;i<6;++i) popup.push_back({static_cast<float>(first+i),2,3,0.25f,0.5f,0x12345678,0x87654321});
            eu4unicode::tag_font_vertices(popup.data()+first,6,page);
        }
        const auto popup_before=popup;
        const auto popup_batches=eu4unicode::split_popup_glyphs(popup,3);
        require(popup_batches.size()==4&&popup_batches[1].first_quad==1&&popup_batches[1].quad_count==2,
                "Popup page runs changed glyph order");
        for(std::size_t i=0;i<popup.size();++i)
            require(popup[i].x==popup_before[i].x&&popup[i].u==0.25f&&popup[i].color==0x12345678&&popup[i].secondary_color==0x87654321,
                    "Popup page decoding changed positions or colors");
        std::cout<<"PASS: adjacent page runs, preserved order and geometry, UV decoding and invalid-input atomicity.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
