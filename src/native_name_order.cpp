#include "native_name_order.hpp"

namespace eu4unicode {
NameAppend native_name_append=nullptr;
NameAssign native_name_assign=nullptr;
bool surname_first(std::string_view suffix,std::size_t& surname_offset) {
    if(suffix.size()>=3&&suffix.substr(0,3)==" \xc2\xbf") {
        surname_offset=3;return true;
    }
    if(suffix.size()>=2&&suffix[0]==' '&&static_cast<unsigned char>(suffix[1])==0xbf) {
        surname_offset=2;return true;
    }
    return false;
}
}

extern "C" void* append_person_name(eu4unicode::EngineString* given,
    const eu4unicode::EngineString* suffix) {
    using namespace eu4unicode;
    const std::string_view family(suffix->data(),static_cast<std::size_t>(suffix->size));
    std::size_t start=0;
    if(!surname_first(family,start))
        return native_name_append(given,family.data(),family.size());
    // Preserve the source before assignment: the engine owns the destination.
    std::string joined(family.substr(start));
    joined.append(given->data(),static_cast<std::size_t>(given->size));
    return native_name_assign(given,joined.data(),joined.size());
}
