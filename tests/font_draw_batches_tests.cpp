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
std::vector<eu4unicode::FontPageSize> equal_pages(std::size_t count) { return {count,{2048,2048}}; }
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
        const auto batches=eu4unicode::split_font_quads(vertices,equal_pages(3));
        require(batches.size()==5&&batches[1].page==1&&batches[1].first_quad==1&&batches[1].quad_count==2,
                "Font draw grouping reordered or merged nonadjacent glyphs");
        for(std::size_t i=0;i<vertices.size();++i) {
            require(vertices[i].x==before[i].x&&vertices[i].y==before[i].y&&vertices[i].z==before[i].z&&vertices[i].v==before[i].v,
                    "Page decoding changed native positions or vertical UVs");
            require(vertices[i].u==(i%2?0.875f:0.125f),"Page decoding changed the physical UV");
        }
        auto missing=quads({0,2});bool rejected=false;
        try { eu4unicode::split_font_quads(missing,equal_pages(2)); } catch(const std::out_of_range&) { rejected=true; }
        require(rejected&&missing[4].u==4.125f,"Unavailable page changed cached geometry");
        auto mixed=quads({1});mixed[3].u=0.5f;rejected=false;
        try { eu4unicode::split_font_quads(mixed,equal_pages(2)); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected&&mixed[0].u==2.125f,"A quad spanning pages was accepted or partially decoded");
        auto invalid=quads({0});invalid[0].u=std::nanf("");rejected=false;
        try { eu4unicode::split_font_quads(invalid,equal_pages(1)); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected,"Non-finite page coordinate was accepted");
        std::vector<eu4unicode::PopupFontVertex> popup;
        for(const auto page:{0u,1u,1u,0u,2u}) {
            const auto first=popup.size();
            for(unsigned i=0;i<6;++i) popup.push_back({static_cast<float>(first+i),2,3,0.25f,0.5f,0x12345678,0x87654321});
            eu4unicode::tag_font_vertices(popup.data()+first,6,page);
        }
        const auto popup_before=popup;
        const auto popup_batches=eu4unicode::split_popup_glyphs(popup,equal_pages(3));
        require(popup_batches.size()==4&&popup_batches[1].first_quad==1&&popup_batches[1].quad_count==2,
                "Popup page runs changed glyph order");
        for(std::size_t i=0;i<popup.size();++i)
            require(popup[i].x==popup_before[i].x&&popup[i].u==0.25f&&popup[i].color==0x12345678&&popup[i].secondary_color==0x87654321,
                    "Popup page decoding changed positions or colors");
        const auto supplement=eu4unicode::supplemental_font_page_size({6400,8192});
        require(supplement.width==2048&&supplement.height==2048&&supplement.rgba_bytes()==16ull*1024*1024,
                "Large mod atlas produces an oversized supplemental page");
        const auto small=eu4unicode::supplemental_font_page_size({128,4096});
        require(small.width==128&&small.height==2048,"Small native dimensions were exceeded by supplemental pages");
        eu4unicode::check_font_page_budget(0,0,supplement);
        eu4unicode::check_font_page_budget(15*supplement.rgba_bytes(),0,supplement);
        rejected=false;
        try { eu4unicode::check_font_page_budget(16*supplement.rgba_bytes(),0,supplement); }
        catch(const std::length_error&) { rejected=true; }
        require(rejected,"Supplemental GPU reservations exceed the memory budget");
        rejected=false;
        try { eu4unicode::check_font_page_budget(15*supplement.rgba_bytes(),32ull*1024*1024,supplement); }
        catch(const std::length_error&) { rejected=true; }
        require(rejected,"Initial staging pixels were omitted from the CPU texture budget");
        const std::vector<eu4unicode::FontPageSize> sizes{{6400,8192},{2048,2048},{1024,512}};
        auto scaled=quads({0,1,2});
        for(std::size_t i=4;i<scaled.size();++i) {
            const auto page=i/4;const auto tag=static_cast<float>(page)*2;
            scaled[i].u=tag+(i%2?768.f:128.f)/6400;
            scaled[i].v=(i%4>=2?384.f:64.f)/8192;
        }
        const auto scaled_before=scaled;
        const auto scaled_batches=eu4unicode::split_font_quads(scaled,sizes);
        require(scaled_batches.size()==3,"Mixed-size pages were reordered");
        for(std::size_t i=0;i<scaled.size();++i) {
            require(scaled[i].x==scaled_before[i].x&&scaled[i].y==scaled_before[i].y&&scaled[i].z==scaled_before[i].z,
                    "Per-page UV scaling changed native map geometry");
            if(i<4) require(scaled[i].u==scaled_before[i].u&&scaled[i].v==scaled_before[i].v,
                            "Original mod UVs were changed");
            else require(std::abs(scaled[i].u-(i%2?768.f:128.f)/sizes[i/4].width)<0.000001f&&
                         std::abs(scaled[i].v-(i%4>=2?384.f:64.f)/sizes[i/4].height)<0.000001f,
                         "Map UVs were not normalized to the selected page");
        }
        std::vector<eu4unicode::PopupFontVertex> scaled_popup(6,{1,2,3,128.f/6400,64.f/8192,0x12345678,0x87654321});
        eu4unicode::tag_font_vertices(scaled_popup.data(),scaled_popup.size(),2);
        eu4unicode::split_popup_glyphs(scaled_popup,sizes);
        for(const auto& v:scaled_popup)
            require(std::abs(v.u-0.125f)<0.000001f&&v.v==0.125f&&v.color==0x12345678&&v.secondary_color==0x87654321,
                    "Popup UV scaling changed native colors or selected the wrong dimensions");
        auto out_of_page=quads({0,1});const auto unchanged=out_of_page;rejected=false;
        try { eu4unicode::split_font_quads(out_of_page,sizes); } catch(const std::invalid_argument&) { rejected=true; }
        require(rejected&&out_of_page[4].u==unchanged[4].u&&out_of_page[0].u==unchanged[0].u,
                "UVs outside a smaller page were accepted or partially transformed");
        std::cout<<"PASS: page sizes, separate budgets, mixed-page UVs, preserved geometry/colors and invalid-input atomicity.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
