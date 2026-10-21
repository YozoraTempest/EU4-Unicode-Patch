#include "formatted_text_cache.hpp"

namespace eu4unicode {
std::shared_ptr<const FormattedText> FormattedTextCache::get(std::string_view text) {
    const auto found=index_.find(text);
    if(found!=index_.end()) {
        entries_.splice(entries_.begin(),entries_,found->second);
        return found->second->value;
    }

    auto value=std::make_shared<const FormattedText>(text);
    if(!entry_limit_||value->memory_size()>byte_limit_||
       sizeof(Entry)>byte_limit_-value->memory_size()) return value;
    entries_.push_front({std::string(text),value,0});
    auto& entry=entries_.front();
    const auto remaining=byte_limit_-value->memory_size()-sizeof(Entry);
    if(entry.text.capacity()>=remaining) {
        entries_.pop_front();return value;
    }
    entry.bytes=sizeof(Entry)+entry.text.capacity()+1+value->memory_size();
    try {
        index_.emplace(std::string_view(entry.text),entries_.begin());
    } catch(...) {
        entries_.pop_front();throw;
    }
    while(index_.size()>entry_limit_||bytes_>byte_limit_-entry.bytes) {
        const auto& oldest=entries_.back();
        index_.erase(std::string_view(oldest.text));
        bytes_-=oldest.bytes;
        entries_.pop_back();
    }
    bytes_+=entry.bytes;
    return value;
}
}
