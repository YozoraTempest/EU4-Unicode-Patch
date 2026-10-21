#pragma once
#include "formatted_text.hpp"
#include <list>
#include <memory>
#include <unordered_map>

namespace eu4unicode {
class FormattedTextCache {
public:
    explicit FormattedTextCache(std::size_t byte_limit=1024*1024,std::size_t entry_limit=1024)
        :byte_limit_(byte_limit),entry_limit_(entry_limit) {}
    FormattedTextCache(const FormattedTextCache&)=delete;
    FormattedTextCache& operator=(const FormattedTextCache&)=delete;
    FormattedTextCache(FormattedTextCache&&)=delete;
    FormattedTextCache& operator=(FormattedTextCache&&)=delete;

    std::shared_ptr<const FormattedText> get(std::string_view text);
    std::size_t size() const noexcept { return index_.size(); }
    // Retained entries, text storage and boundary data; container bookkeeping
    // is bounded separately by entry_limit_. Callers may keep evicted values.
    std::size_t cached_bytes() const noexcept { return bytes_; }
private:
    struct Entry {
        std::string text;
        std::shared_ptr<const FormattedText> value;
        std::size_t bytes;
    };
    std::size_t byte_limit_,entry_limit_,bytes_=0;
    std::list<Entry> entries_;
    // Views refer to owned list entries. Splicing preserves their addresses.
    std::unordered_map<std::string_view,std::list<Entry>::iterator> index_;
};
}
