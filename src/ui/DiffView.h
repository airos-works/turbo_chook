#pragma once
#include "core/DiffParse.h"
#include "ui/Widgets.h"
#include <Messenger.h>
#include <View.h>
#include <set>
#include <string>
#include <vector>
class BScrollView;
namespace tc {
// Unified diff renderer with per-hunk and per-line staging controls.
// Rows are drawn on demand; the owner supplies the diff text and a mode
// that decides which actions the hunk headers offer.
class DiffView : public BView {
public:
    enum Mode { kReadOnly, kUnstaged, kStaged, kUntracked };
    DiffView();
    void SetTarget(BMessenger target) { fTarget = target; }
    void SetDiff(const std::string& unified, Mode mode, const std::string& title = {});
    void SetMessage(const std::string& message);
    void Clear() { SetMessage(""); }
    const std::vector<DiffFile>& Files() const { return fFiles; }
    Mode CurrentMode() const { return fMode; }
    void AttachedToWindow() override;
    void Draw(BRect updateRect) override;
    void FrameResized(float width, float height) override;
    void MouseDown(BPoint where) override;
    void MouseMoved(BPoint where, uint32 transit, const BMessage* drag) override;
    void KeyDown(const char* bytes, int32 count) override;
    void MessageReceived(BMessage* message) override;
    BSize MinSize() override { return BSize(100, 60); }
    BSize PreferredSize() override { return BSize(600, 400); }
    void ScrollToHunk(int direction);   // +1 next, -1 previous
private:
    struct Row { enum Kind { kFileHeader, kHunkHeader, kLine, kNote } kind; int file = -1, hunk = -1, line = -1; };
    struct Button { BRect frame; uint32 command; int file, hunk; std::string label; };
    void Rebuild();
    void UpdateScrollBars();
    float RowHeight() const { return fRowHeight; }
    int RowAt(BPoint where) const;
    void DrawRow(const Row& row, BRect frame, int index);
    std::vector<Button> ButtonsFor(int index) const;   // hunk header actions, in view coordinates
    void SendHunkAction(uint32 command, int file, int hunk, bool lines);
    std::vector<DiffFile> fFiles;
    std::vector<Row> fRows;
    std::set<int> fSelectedRows;      // row indexes of selected diff lines (one hunk at a time)
    int fSelectionAnchor = -1, fSelectedHunk = -1, fSelectedFile = -1;
    std::string fMessage, fTitle;
    Mode fMode = kReadOnly;
    BMessenger fTarget;
    Palette fPalette;
    BFont fFont, fBoldFont;
    float fRowHeight = 16, fCharWidth = 7, fNumberWidth = 40, fContentWidth = 0;
    int fHoverRow = -1;
};
}
