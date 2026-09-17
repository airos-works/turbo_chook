#pragma once
#include "core/Git.h"
#include <string>
#include <vector>

namespace tc {
// Lane layout for a commit list in topological (or date) order.
//
// Columns are stable: a lane keeps its column for as long as it is open, and a
// freed column stays empty until a later branch reuses it. Nothing slides
// sideways, so unrelated branch lines never drift across the graph.
//
// Each row is split at the commit's node:
//   up   edges run from a column at the top of the row to the node's centre
//        height (a straight pass-through, or a lane converging into the node);
//   down edges run from the centre height to a column at the bottom of the
//        row (a pass-through, or the node leaving towards a parent's lane).
// The columns leaving a row at the bottom are exactly the columns entering the
// next row at the top, with the same colours, so lines are always continuous.
// Colours are indexes into a palette of kGraphColors entries.
constexpr int kGraphColors = 8;
struct GraphEdge { int from = 0, to = 0, color = 0; };
struct GraphRow {
    int lane = 0, color = 0;
    int lanes = 1;                 // columns needed to draw this row
    bool merge = false;            // more than one parent: drawn as a hollow node
    std::vector<GraphEdge> up, down;
};
class CommitGraph {
public:
    GraphRow Append(const Commit& commit);
    void Clear();
    int MaxLanes() const { return fMaxLanes; }
private:
    struct Lane { std::string hash; int color = 0; };
    int FreeColumn(int from);      // first empty column at or after `from`, appending when none
    int NewColor(int column, int avoid);   // a colour unlike the lanes around `column`
    std::vector<Lane> fLanes;      // empty hash: a free column
    int fNextColor = 0, fMaxLanes = 0;
};
std::vector<GraphRow> LayoutGraph(const std::vector<Commit>& commits);
}
