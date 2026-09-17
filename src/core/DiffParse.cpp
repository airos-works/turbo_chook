#include "core/DiffParse.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace tc {
namespace {
std::vector<std::string_view> Lines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t offset = 0;
    while (offset < text.size()) {
        auto end = text.find('\n', offset);
        if (end == std::string_view::npos) end = text.size();
        lines.push_back(text.substr(offset, end - offset));
        offset = end + 1;
    }
    return lines;
}
bool ParseHunkHeader(std::string_view line, DiffHunk& hunk) {
    // @@ -oldStart[,oldCount] +newStart[,newCount] @@ context
    if (line.rfind("@@ -", 0) != 0) return false;
    std::string text(line);
    const char* p = text.c_str() + 4;
    char* end = nullptr;
    hunk.oldStart = std::strtol(p, &end, 10); p = end; hunk.oldCount = 1;
    if (*p == ',') { hunk.oldCount = std::strtol(p + 1, &end, 10); p = end; }
    if (*p++ != ' ' || *p++ != '+') return false;
    hunk.newStart = std::strtol(p, &end, 10); p = end; hunk.newCount = 1;
    if (*p == ',') { hunk.newCount = std::strtol(p + 1, &end, 10); p = end; }
    hunk.header = text;
    return true;
}
std::string StripPrefix(std::string_view path) {
    if (path == "/dev/null") return std::string(path);
    if (path.size() > 2 && (path.rfind("a/", 0) == 0 || path.rfind("b/", 0) == 0)) path.remove_prefix(2);
    auto tab = path.find('\t');
    if (tab != std::string_view::npos) path = path.substr(0, tab);
    return std::string(path);
}
}
int DiffHunk::added() const { return static_cast<int>(std::count_if(lines.begin(), lines.end(), [](const DiffLine& l) { return l.kind == '+'; })); }
int DiffHunk::removed() const { return static_cast<int>(std::count_if(lines.begin(), lines.end(), [](const DiffLine& l) { return l.kind == '-'; })); }
std::vector<DiffFile> ParseUnifiedDiff(std::string_view text) {
    std::vector<DiffFile> files;
    DiffFile* file = nullptr;
    DiffHunk* hunk = nullptr;
    long oldLine = 0, newLine = 0;
    for (auto line : Lines(text)) {
        if (line.rfind("diff --git ", 0) == 0 || line.rfind("diff --cc ", 0) == 0) {
            files.emplace_back();
            file = &files.back(); hunk = nullptr;
            file->headerLines.emplace_back(line);
            // "diff --git a/x b/y": recover paths for files without hunks (binary, mode only).
            std::string rest(line.substr(11));
            auto split = rest.find(" b/");
            if (split != std::string::npos) { file->oldPath = rest.substr(2, split - 2); file->newPath = rest.substr(split + 3); }
            continue;
        }
        if (!file) {
            if (line.rfind("--- ", 0) == 0) { files.emplace_back(); file = &files.back(); }
            else continue;
        }
        if (hunk == nullptr || line.rfind("@@", 0) == 0) {
            if (line.rfind("--- ", 0) == 0) { file->oldPath = StripPrefix(line.substr(4)); file->headerLines.emplace_back(line); if (file->oldPath == "/dev/null") file->added = true; continue; }
            if (line.rfind("+++ ", 0) == 0) { file->newPath = StripPrefix(line.substr(4)); file->headerLines.emplace_back(line); if (file->newPath == "/dev/null") file->deleted = true; continue; }
            if (line.rfind("@@", 0) == 0) {
                DiffHunk h;
                if (!ParseHunkHeader(line, h)) continue;
                file->hunks.push_back(std::move(h));
                hunk = &file->hunks.back();
                oldLine = hunk->oldStart; newLine = hunk->newStart;
                continue;
            }
            if (line.rfind("Binary files", 0) == 0 || line.rfind("GIT binary patch", 0) == 0) { file->binary = true; file->headerLines.emplace_back(line); continue; }
            if (line.rfind("rename from ", 0) == 0) { file->renamed = true; file->oldPath = std::string(line.substr(12)); }
            if (line.rfind("rename to ", 0) == 0) { file->renamed = true; file->newPath = std::string(line.substr(10)); }
            if (line.rfind("new file mode", 0) == 0) file->added = true;
            if (line.rfind("deleted file mode", 0) == 0) file->deleted = true;
            file->headerLines.emplace_back(line);
            continue;
        }
        DiffLine entry;
        if (line.empty()) { entry.kind = ' '; }
        else { entry.kind = line[0]; entry.text = std::string(line.substr(1)); }
        if (entry.kind == '\\') { hunk->lines.push_back(std::move(entry)); continue; }
        if (entry.kind != ' ' && entry.kind != '+' && entry.kind != '-') {
            // Not part of the diff body (for example trailing garbage). End the hunk.
            hunk = nullptr; continue;
        }
        if (entry.kind != '+') entry.oldNumber = oldLine++;
        if (entry.kind != '-') entry.newNumber = newLine++;
        hunk->lines.push_back(std::move(entry));
    }
    return files;
}
std::string FormatHunkHeader(const DiffHunk& hunk) {
    std::string header = "@@ -" + std::to_string(hunk.oldStart);
    if (hunk.oldCount != 1) header += "," + std::to_string(hunk.oldCount);
    header += " +" + std::to_string(hunk.newStart);
    if (hunk.newCount != 1) header += "," + std::to_string(hunk.newCount);
    header += " @@";
    auto at = hunk.header.find(" @@");
    if (at != std::string::npos && at + 3 < hunk.header.size()) header += hunk.header.substr(at + 3);
    return header;
}
static std::string PatchHeader(const DiffFile& file) {
    std::string patch;
    bool sawOld = false, sawNew = false;
    for (const auto& line : file.headerLines) {
        if (line.rfind("Binary files", 0) == 0 || line.rfind("GIT binary patch", 0) == 0) continue;
        if (line.rfind("--- ", 0) == 0) sawOld = true;
        if (line.rfind("+++ ", 0) == 0) sawNew = true;
        patch += line; patch += '\n';
    }
    if (!sawOld) patch += (file.added ? "--- /dev/null\n" : "--- a/" + file.oldPath + "\n");
    if (!sawNew) patch += (file.deleted ? "+++ /dev/null\n" : "+++ b/" + file.newPath + "\n");
    return patch;
}
std::string HunkPatch(const DiffFile& file, const DiffHunk& hunk, const std::vector<size_t>& selected) {
    DiffHunk out = hunk;
    if (!selected.empty()) {
        out.lines.clear();
        for (size_t i = 0; i < hunk.lines.size(); ++i) {
            const auto& line = hunk.lines[i];
            bool chosen = std::find(selected.begin(), selected.end(), i) != selected.end();
            if (line.kind == '\\') {
                // Keep the marker only when its preceding line survives.
                if (!out.lines.empty()) out.lines.push_back(line);
                continue;
            }
            if (line.kind == ' ' || chosen) { out.lines.push_back(line); continue; }
            if (line.kind == '-') { DiffLine context = line; context.kind = ' '; out.lines.push_back(context); }
            // unselected '+' lines are omitted entirely
        }
    }
    long oldCount = 0, newCount = 0;
    for (const auto& line : out.lines) {
        if (line.kind == ' ') { ++oldCount; ++newCount; }
        else if (line.kind == '-') ++oldCount;
        else if (line.kind == '+') ++newCount;
    }
    out.oldCount = oldCount; out.newCount = newCount;
    if (oldCount == 0) out.oldStart = hunk.oldStart == 0 ? 0 : hunk.oldStart - 1 + (hunk.oldCount == 0 ? 1 : 0);
    if (oldCount == 0 && hunk.oldCount > 0) out.oldStart = hunk.oldStart - 1;
    if (out.oldCount == 0 && hunk.oldCount == 0) out.oldStart = hunk.oldStart;
    if (newCount == 0) out.newStart = hunk.newStart > 0 ? hunk.newStart - 1 : 0;
    if (newCount == 0 && hunk.newCount == 0) out.newStart = hunk.newStart;
    std::string patch = PatchHeader(file);
    patch += FormatHunkHeader(out); patch += '\n';
    for (const auto& line : out.lines) {
        patch += line.kind; patch += line.text; patch += '\n';
    }
    return patch;
}
std::string FilePatch(const DiffFile& file) {
    std::string patch = PatchHeader(file);
    for (const auto& hunk : file.hunks) {
        patch += FormatHunkHeader(hunk); patch += '\n';
        for (const auto& line : hunk.lines) { patch += line.kind; patch += line.text; patch += '\n'; }
    }
    return patch;
}
}
