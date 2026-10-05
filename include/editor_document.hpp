#pragma once
#include "unicode_editor.hpp"
#include <deque>
#include <optional>
#include <vector>

namespace eu4unicode {
struct EditRowInput { std::string_view text;bool synthetic_newline=false; };
struct EditRow { std::size_t start,length,consumed; };
struct EditPosition { std::size_t row,column; };
// The engine inserts newlines for soft wraps in a separate row cache. Source
// positions always address the original UTF-8, including real CR/LF bytes.
class EditRows {
public:
    EditRows(std::string_view source,const std::vector<EditRowInput>& rows);
    const std::vector<EditRow>& rows() const noexcept { return rows_; }
    std::size_t offset(EditPosition position) const;
    EditPosition position(std::size_t offset) const;
private:
    std::vector<EditRow> rows_;
    std::size_t bytes_=0;
};
struct EditState { std::string text;Selection selection; };
class EditHistory {
public:
    void record(const EditState& before,const EditState& after);
    std::optional<EditState> undo(const EditState& current);
    std::optional<EditState> redo(const EditState& current);
    void clear() noexcept;
    std::size_t bytes() const noexcept { return bytes_; }
private:
    struct Change { EditState before,after; };
    std::deque<Change> changes_;
    std::size_t index_=0,bytes_=0;
    bool synchronize(const EditState& current);
};
struct CompositionText { std::string text;std::size_t caret=0; };
// IMM cursor positions are UTF-16 units; the editor uses UTF-8 byte positions.
CompositionText composition_text(std::u16string_view text,std::size_t caret);
struct CompositionPreview { std::string text;std::size_t caret,begin,end; };
CompositionPreview composition_preview(const EditState& source,const CompositionText& composition,
                                       std::size_t byte_limit=32000);
}
