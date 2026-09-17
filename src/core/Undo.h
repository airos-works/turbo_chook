#pragma once
#include "core/Process.h"
#include <functional>
#include <string>
#include <vector>

namespace tc {
// Tower-style "undo anything": every destructive operation records how to
// reverse itself before it runs. Entries are per repository window.
struct UndoEntry {
    std::string description;                    // "Undo delete branch 'feature'"
    std::function<ProcessResult()> undo;
};
class UndoStack {
public:
    void Push(std::string description, std::function<ProcessResult()> undo) {
        fEntries.push_back({std::move(description), std::move(undo)});
        if (fEntries.size() > 50) fEntries.erase(fEntries.begin());
    }
    bool Empty() const { return fEntries.empty(); }
    const std::string& Description() const { return fEntries.back().description; }
    ProcessResult Pop() {
        auto entry = std::move(fEntries.back());
        fEntries.pop_back();
        return entry.undo();
    }
    void Clear() { fEntries.clear(); }
    size_t Size() const { return fEntries.size(); }
private:
    std::vector<UndoEntry> fEntries;
};
}
