#include "core/Graph.h"
#include <algorithm>

namespace tc {
void CommitGraph::Clear() { fLanes.clear(); fNextColor = 0; fMaxLanes = 0; }
int CommitGraph::FreeColumn(int from) {
    for (int i = std::max(0, from); i < static_cast<int>(fLanes.size()); ++i)
        if (fLanes[i].hash.empty()) return i;
    fLanes.push_back({});
    return static_cast<int>(fLanes.size()) - 1;
}
int CommitGraph::NewColor(int column, int avoid) {
    // Prefer a colour no nearby open lane uses, so adjacent lines stay distinguishable.
    bool used[kGraphColors] = {};
    if (avoid >= 0) used[avoid % kGraphColors] = true;
    for (int i = std::max(0, column - 3); i < std::min(static_cast<int>(fLanes.size()), column + 4); ++i)
        if (!fLanes[i].hash.empty()) used[fLanes[i].color % kGraphColors] = true;
    for (int step = 0; step < kGraphColors; ++step) {
        int candidate = (fNextColor + step) % kGraphColors;
        if (!used[candidate]) { fNextColor = candidate + 1; return candidate; }
    }
    return fNextColor++ % kGraphColors;
}
GraphRow CommitGraph::Append(const Commit& commit) {
    GraphRow row;
    row.merge = commit.parents.size() > 1;
    const std::vector<Lane> top = fLanes;

    // The node takes the leftmost column waiting for it; other columns waiting
    // for the same commit converge into it. A branch tip nobody waits for takes
    // the first free column.
    std::vector<int> waiting;
    for (int i = 0; i < static_cast<int>(top.size()); ++i)
        if (top[i].hash == commit.hash) waiting.push_back(i);
    if (waiting.empty()) {
        row.lane = FreeColumn(0);
        row.color = NewColor(row.lane, -1);
    } else {
        row.lane = waiting.front();
        row.color = top[row.lane].color;
    }
    for (int i = 0; i < static_cast<int>(top.size()); ++i) {
        if (top[i].hash.empty()) continue;
        if (top[i].hash == commit.hash) row.up.push_back({i, row.lane, top[i].color});
        else row.up.push_back({i, i, top[i].color});
    }
    for (int i : waiting) fLanes[i] = {};
    fLanes[row.lane] = {};

    // Parents: one already waited for elsewhere gets a link into that column.
    // Otherwise the first parent continues straight down in the node's column
    // and further parents take the nearest free column to the right.
    for (size_t p = 0; p < commit.parents.size(); ++p) {
        const std::string& parent = commit.parents[p];
        int column = -1;
        for (int i = 0; i < static_cast<int>(fLanes.size()); ++i)
            if (fLanes[i].hash == parent) { column = i; break; }
        // The first parent is already awaited by a column to the right: keep this
        // column running straight instead of jumping over, and let the right-hand
        // column converge into it at the parent. Main lines stay on the left.
        if (column > row.lane && p == 0 && fLanes[row.lane].hash.empty()) {
            fLanes[row.lane] = {parent, row.color};
            column = row.lane;
        }
        if (column < 0) {
            column = fLanes[row.lane].hash.empty() ? row.lane : FreeColumn(row.lane + 1);
            fLanes[column] = {parent, p == 0 && column == row.lane ? row.color : NewColor(column, row.color)};
        }
        row.down.push_back({row.lane, column, fLanes[column].color});
    }
    // Every other open column that was already open above runs straight through.
    // (Columns opened by this row are reached by the parent links themselves.)
    for (int i = 0; i < static_cast<int>(fLanes.size()) && i < static_cast<int>(top.size()); ++i) {
        if (i == row.lane || fLanes[i].hash.empty() || top[i].hash != fLanes[i].hash) continue;
        row.down.push_back({i, i, fLanes[i].color});
    }
    while (!fLanes.empty() && fLanes.back().hash.empty()) fLanes.pop_back();

    int widest = row.lane + 1;
    for (const auto& e : row.up) widest = std::max({widest, e.from + 1, e.to + 1});
    for (const auto& e : row.down) widest = std::max({widest, e.from + 1, e.to + 1});
    row.lanes = widest;
    fMaxLanes = std::max(fMaxLanes, widest);
    return row;
}
std::vector<GraphRow> LayoutGraph(const std::vector<Commit>& commits) {
    CommitGraph graph;
    std::vector<GraphRow> rows;
    rows.reserve(commits.size());
    for (const auto& c : commits) rows.push_back(graph.Append(c));
    return rows;
}
}
