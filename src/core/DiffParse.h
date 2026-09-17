#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace tc {
// A unified diff broken into files, hunks and lines. Line text keeps its
// leading marker character (' ', '+', '-', '\\') so patches can be rebuilt.
struct DiffLine {
    char kind = ' ';           // ' ' context, '+' added, '-' removed, '\\' no-newline marker
    std::string text;          // without the marker
    long oldNumber = 0, newNumber = 0;   // 0 when not applicable
};
struct DiffHunk {
    long oldStart = 0, oldCount = 0, newStart = 0, newCount = 0;
    std::string header;        // the full "@@ ... @@ context" line
    std::vector<DiffLine> lines;
    int added() const; int removed() const;
};
struct DiffFile {
    std::string oldPath, newPath;
    std::vector<std::string> headerLines;   // "diff --git", "index", "---", "+++", mode lines...
    std::vector<DiffHunk> hunks;
    bool binary = false, added = false, deleted = false, renamed = false;
    std::string displayPath() const { return newPath.empty() || newPath == "/dev/null" ? oldPath : newPath; }
};
std::vector<DiffFile> ParseUnifiedDiff(std::string_view text);
// Rebuilds a patch containing only one hunk of one file, suitable for
// `git apply`. When `selected` is non-empty only those line indexes (into
// hunk.lines) are kept as changes; unselected '+' lines are dropped and
// unselected '-' lines become context. Counts are recomputed.
std::string HunkPatch(const DiffFile& file, const DiffHunk& hunk, const std::vector<size_t>& selected = {});
std::string FilePatch(const DiffFile& file);
std::string FormatHunkHeader(const DiffHunk& hunk);
}
