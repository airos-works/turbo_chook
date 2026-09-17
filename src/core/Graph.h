#pragma once
#include "core/Git.h"
#include <string>
#include <vector>

namespace tc {
// Lane layout for a commit list in topological order. Each row records the
// commit's own lane plus the segments that run through it from the row above
// to the row below, so the view can draw a railway-style graph.
struct GraphSegment { int fromLane = 0, toLane = 0, color = 0; bool starts = false, ends = false; };
struct GraphRow {
    int lane = 0, color = 0, lanes = 1;   // lanes: total lanes in use at this row
    bool merge = false;                   // more than one parent: drawn as a hollow node
    std::vector<GraphSegment> segments;
};
class CommitGraph {
public:
    GraphRow Append(const Commit& commit);
    void Clear();
    int MaxLanes() const { return fMaxLanes; }
private:
    struct Lane { std::string hash; int color = 0; bool active = true; };
    std::vector<Lane> fLanes;
    int fNextColor = 0, fMaxLanes = 0;
};
std::vector<GraphRow> LayoutGraph(const std::vector<Commit>& commits);
}
