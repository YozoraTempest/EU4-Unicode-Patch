#include "font_assets.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    using eu4unicode::player_font_path;
    try {
        if(player_font_path("gfx/fonts/vic_18s")!="gfx/fonts/eu4-unicode/zh-hans-16"||
           player_font_path("gfx/fonts/garamond_16_bold")!="gfx/fonts/eu4-unicode/zh-hans-16"||
           player_font_path("gfx/fonts/Mapfont")!="gfx/fonts/eu4-unicode/zh-hans-map"||
           player_font_path("gfx/fonts/zh-hans-24")!="gfx/fonts/eu4-unicode/zh-hans-24")
            throw std::runtime_error("Native UI/map font mapping failed");
        for(const auto path:{"gfx/fonts/icons","mod/vic_18","gfx/fonts/vic_18_extra",
                             "gfx/fonts/eu4-unicode/zh-hans-16",""})
            if(!player_font_path(path).empty()) throw std::runtime_error("Unrelated resource was redirected");
        std::cout<<"PASS: text font aliases map to shared private atlases; custom resources remain native.\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
