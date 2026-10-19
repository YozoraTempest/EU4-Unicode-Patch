#include "native_yaml.hpp"
#include "yaml_input.hpp"
#include <atomic>

namespace eu4unicode {
NativeYamlParse original_yaml_parse=nullptr;
namespace {std::atomic<std::uint64_t> signatures{0};}
void parse_utf8_yaml(void* file,const NativeYamlBytes* input,const char* name,
                     NativeYamlLanguage language,NativeYamlKey key,void* context) {
    const auto size=input->begin?static_cast<std::size_t>(input->end-input->begin):0;
    const auto bytes=std::string_view(input->begin,size);
    if(const auto normalized=yaml_parse_buffer(bytes)) {
        const auto begin=normalized->data();
        const NativeYamlBytes buffer{begin,begin+normalized->size(),begin+normalized->size()};
        signatures.fetch_add(1,std::memory_order_relaxed);
        original_yaml_parse(file,&buffer,name,language,key,context);
    } else original_yaml_parse(file,input,name,language,key,context);
}
std::uint64_t native_yaml_signatures() noexcept {return signatures.load(std::memory_order_relaxed);}
}
