#include "font_assets.hpp"
#include <array>
#include <utility>

namespace eu4unicode {
std::string_view player_font_path(std::string_view path) noexcept {
    constexpr std::string_view prefix="gfx/fonts/";
    if(path.substr(0,prefix.size())!=prefix) return {};
    path.remove_prefix(prefix.size());
    constexpr std::array<std::pair<std::string_view,int>,29> names{{
        {"Arial12",14},{"Arial12_bold",14},{"garamond_12",14},
        {"garamond_14",14},{"garamond_14_bold",14},{"garamond_16",16},
        {"garamond_16_bold",16},{"garamond_24",24},{"Mapfont",88},
        {"standard",14},{"standard_18",14},{"standard_22",14},
        {"tahoma_20_bold",14},{"tahoma_60",88},
        {"vic_18",16},{"vic_18_grey",16},{"vic_18s",16},
        {"vic_22",18},{"vic_22_bl",18},{"vic_22s",18},
        {"vic_29",24},{"vic_29s",24},{"vic_36",24},{"vic_36s",24},
        {"zh-hans-14",14},{"zh-hans-16",16},{"zh-hans-18",18},
        {"zh-hans-24",24},{"zh-hans-map",88}
    }};
    for(const auto& name:names) if(path==name.first) {
        switch(name.second) {
            case 14:return "gfx/fonts/eu4-unicode/cache/zh-hans-14";
            case 16:return "gfx/fonts/eu4-unicode/cache/zh-hans-16";
            case 18:return "gfx/fonts/eu4-unicode/cache/zh-hans-18";
            case 24:return "gfx/fonts/eu4-unicode/cache/zh-hans-24";
            case 88:return "gfx/fonts/eu4-unicode/cache/zh-hans-map";
        }
    }
    return {};
}
}
