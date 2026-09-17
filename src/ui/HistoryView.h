#pragma once
#include "core/Git.h"
#include "core/Graph.h"
#include "ui/Widgets.h"
#include <Messenger.h>
#include <View.h>
#include <set>
#include <string>
#include <vector>
namespace tc {
// The commit list with a railway graph, ref badges, author and date columns.
// Commits are drag sources (cherry-pick, branch and tag creation on drop).
class HistoryView : public BView {
public:
    HistoryView();
    void SetTarget(BMessenger target) { fTarget = target; }
    // append adds a page; keepPosition replaces the list without touching scroll or selection.
    void SetCommits(std::vector<Commit> commits, bool append, bool keepPosition = false);
    void SetFilter(const std::string& filter);
    void SetCurrentHead(const std::string& hash) { fHead = hash; Invalidate(); }
    void SetLoading(bool loading, bool more);
    void Restyle();
    std::vector<std::string> SelectedHashes() const;
    const Commit* CommitFor(const std::string& hash) const;
    void SelectHash(const std::string& hash, bool notify = true);
    size_t Count() const { return fCommits.size(); }
    void AttachedToWindow() override;
    void Draw(BRect updateRect) override;
    void FrameResized(float width, float height) override;
    void MouseDown(BPoint where) override;
    void MouseUp(BPoint where) override;
    void MouseMoved(BPoint where, uint32 transit, const BMessage* drag) override;
    void KeyDown(const char* bytes, int32 count) override;
    void MessageReceived(BMessage* message) override;
    BSize MinSize() override { return BSize(200, 60); }
    BSize PreferredSize() override { return BSize(700, 300); }
private:
    void Relayout();
    void UpdateScrollBars();
    int RowAt(BPoint where) const;
    void DrawGraph(const GraphRow& row, BRect cell, bool selected);
    void DrawBadge(const std::string& text, rgb_color fill, float& x, float baseline, float top, float bottom);
    void NotifySelection();
    void ShowContextMenu(BPoint screen, int index);
    rgb_color LaneColor(int index) const;
    void StartDrag(BPoint where);
    void EnsureVisible(int index);
    std::vector<Commit> fCommits;
    std::vector<GraphRow> fRows;
    std::vector<int> fVisible;         // indexes into fCommits after filtering
    std::set<int> fSelected;           // indexes into fCommits
    int fAnchor = -1, fLead = -1;
    std::string fFilter, fHead;
    bool fLoading = false, fMore = false, fMoreRequested = false;
    BMessenger fTarget;
    Palette fPalette;
    float fRowHeight = 22, fGraphWidth = 60;
    BPoint fPressed; bool fDragArmed = false; int fPressedIndex = -1;
};
}
