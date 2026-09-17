#include "core/Graph.h"
#include <algorithm>

namespace tc {
void CommitGraph::Clear() { fLanes.clear(); fNextColor = 0; fMaxLanes = 0; }
GraphRow CommitGraph::Append(const Commit& commit) {
    GraphRow row;
    // Lanes waiting for this commit: the first becomes its lane, the others
    // end here with a segment converging into it.
    std::vector<int> waiting;
    for (size_t i = 0; i < fLanes.size(); ++i) if (fLanes[i].hash == commit.hash) waiting.push_back(static_cast<int>(i));
    if (waiting.empty()) {
        auto free = std::find_if(fLanes.begin(), fLanes.end(), [](const Lane& l) { return l.hash.empty(); });
        if (free == fLanes.end()) { fLanes.push_back({commit.hash, fNextColor++, true}); free = fLanes.end() - 1; }
        else *free = {commit.hash, fNextColor++, true};
        waiting.push_back(static_cast<int>(free - fLanes.begin()));
    }
    row.lane = waiting.front();
    row.color = fLanes[row.lane].color;
    const std::vector<Lane> before = fLanes;
    std::vector<Lane> after = fLanes;
    for (int lane : waiting) after[lane].hash.clear();
    // First parent continues in this lane; further parents take the next free
    // lane to the right, unless some lane already waits for them.
    std::vector<int> parentLanes;
    for (size_t p = 0; p < commit.parents.size(); ++p) {
        const auto& parent = commit.parents[p];
        auto existing = std::find_if(after.begin(), after.end(), [&](const Lane& l) { return l.hash == parent; });
        if (existing != after.end()) { parentLanes.push_back(static_cast<int>(existing - after.begin())); continue; }
        if (p == 0) { after[row.lane] = {parent, row.color, true}; parentLanes.push_back(row.lane); continue; }
        auto free = std::find_if(after.begin() + row.lane, after.end(), [](const Lane& l) { return l.hash.empty(); });
        if (free == after.end()) { after.push_back({parent, fNextColor++, true}); free = after.end() - 1; }
        else *free = {parent, fNextColor++, true};
        parentLanes.push_back(static_cast<int>(free - after.begin()));
    }
    // Compact: empty lanes disappear and everything to their right shifts left.
    std::vector<int> bottom(after.size(), -1);
    std::vector<Lane> compacted;
    for (size_t i = 0; i < after.size(); ++i) {
        if (after[i].hash.empty()) continue;
        bottom[i] = static_cast<int>(compacted.size());
        compacted.push_back(after[i]);
    }
    for (size_t i = 0; i < before.size(); ++i) {
        if (before[i].hash.empty() || before[i].hash == commit.hash) continue;
        row.segments.push_back({static_cast<int>(i), bottom[i], before[i].color, false, false});
    }
    for (int lane : waiting) if (lane != row.lane) row.segments.push_back({lane, row.lane, before[lane].color, false, true});
    for (int lane : parentLanes) row.segments.push_back({row.lane, bottom[lane], after[lane].color, true, false});
    fLanes = compacted;
    row.lanes = std::max<int>({static_cast<int>(before.size()), static_cast<int>(compacted.size()), row.lane + 1});
    fMaxLanes = std::max(fMaxLanes, row.lanes);
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
