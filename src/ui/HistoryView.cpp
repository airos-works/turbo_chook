#include "ui/HistoryView.h"
#include "ui/Messages.h"
#include "ui/Widgets.h"
#include <Bitmap.h>
#include <ScrollBar.h>
#include <Window.h>
#include <algorithm>
#include <cmath>

namespace tc {
namespace {
const rgb_color kLaneColors[] = {
    {66, 133, 244, 255}, {52, 168, 83, 255}, {234, 67, 53, 255}, {251, 188, 4, 255},
    {155, 81, 224, 255}, {0, 172, 193, 255}, {255, 112, 67, 255}, {121, 85, 72, 255}};
const float kLaneWidth = 14, kNodeRadius = 4.5f, kGraphInset = 10, kLinkOffset = 2;
const float kAuthorWidth = 130, kDateWidth = 100;
bool Matches(const Commit& c, const std::string& filter) {
    if (filter.empty()) return true;
    auto lower = [](std::string s) { for (auto& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); return s; };
    std::string f = lower(filter);
    return lower(c.subject).find(f) != std::string::npos || lower(c.authorName).find(f) != std::string::npos
        || c.hash.rfind(f, 0) == 0 || lower(c.body).find(f) != std::string::npos;
}
}
HistoryView::HistoryView()
    : BView("history", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE), fPalette(Palette::Current()) {
    SetViewColor(B_TRANSPARENT_COLOR);
}
void HistoryView::AttachedToWindow() {
    BView::AttachedToWindow();
    Restyle();
}
void HistoryView::Restyle() {
    fPalette = Palette::Current();
    font_height fh; PlainFont().GetHeight(&fh);
    fRowHeight = std::ceil(fh.ascent + fh.descent + fh.leading) + 8;
    Relayout();
}
void HistoryView::SetCommits(std::vector<Commit> commits, bool append, bool keepPosition) {
    if (append) fCommits.insert(fCommits.end(), commits.begin(), commits.end());
    else if (keepPosition) {
        // Re-map the selection by hash so a refreshed list keeps what the user chose.
        std::set<std::string> selected;
        for (int index : fSelected) if (index >= 0 && index < static_cast<int>(fCommits.size())) selected.insert(fCommits[index].hash);
        std::string lead = fLead >= 0 && fLead < static_cast<int>(fCommits.size()) ? fCommits[fLead].hash : "";
        fCommits = std::move(commits);
        fSelected.clear(); fAnchor = fLead = -1;
        for (size_t i = 0; i < fCommits.size(); ++i) {
            if (selected.count(fCommits[i].hash)) fSelected.insert(static_cast<int>(i));
            if (fCommits[i].hash == lead) fAnchor = fLead = static_cast<int>(i);
        }
    } else { fCommits = std::move(commits); fSelected.clear(); fAnchor = fLead = -1; ScrollTo(0, 0); }
    fRows = LayoutGraph(fCommits);
    fMoreRequested = false;
    Relayout();
}
void HistoryView::SetFilter(const std::string& filter) { fFilter = filter; Relayout(); }
void HistoryView::SetLoading(bool loading, bool more) { fLoading = loading; fMore = more; if (!loading) fMoreRequested = false; Invalidate(); }
void HistoryView::Relayout() {
    fVisible.clear();
    std::vector<int> widths;
    widths.reserve(fCommits.size());
    for (size_t i = 0; i < fCommits.size(); ++i) {
        if (Matches(fCommits[i], fFilter)) fVisible.push_back(static_cast<int>(i));
        if (i < fRows.size()) widths.push_back(fRows[i].lanes);
    }
    // Size the column for nearly every row; the rare very wide rows are clipped
    // with a fade instead of pushing every subject off to the right.
    int lanes = 1;
    if (!widths.empty()) {
        std::sort(widths.begin(), widths.end());
        lanes = std::max(1, widths[std::min(widths.size() - 1, widths.size() * 95 / 100)]);
    }
    float cap = std::max(120.0f, Bounds().Width() * 0.32f);
    fGraphWidth = fFilter.empty() ? std::min(cap, kGraphInset + (lanes - 1) * kLaneWidth + kGraphInset + 4) : 12;
    UpdateScrollBars();
    Invalidate();
}
void HistoryView::UpdateScrollBars() {
    float height = (fVisible.size() + (fMore || fLoading ? 1 : 0)) * fRowHeight;
    if (BScrollBar* bar = ScrollBar(B_VERTICAL)) {
        // Keep the current position; only the range changes (BScrollBar clamps the value).
        bar->SetRange(0, std::max(0.0f, height - Bounds().Height()));
        bar->SetSteps(fRowHeight, Bounds().Height() - fRowHeight);
        bar->SetProportion(height > 0 ? std::min(1.0f, Bounds().Height() / height) : 1.0f);
    }
}
void HistoryView::FrameResized(float, float) { Relayout(); }
int HistoryView::RowAt(BPoint where) const {
    int index = static_cast<int>(std::floor(where.y / fRowHeight));
    return index >= 0 && index < static_cast<int>(fVisible.size()) ? index : -1;
}
rgb_color HistoryView::LaneColor(int index) const { return kLaneColors[index % (sizeof(kLaneColors) / sizeof(kLaneColors[0]))]; }
std::vector<std::string> HistoryView::SelectedHashes() const {
    std::vector<std::string> hashes;
    for (int index : fSelected) if (index >= 0 && index < static_cast<int>(fCommits.size())) hashes.push_back(fCommits[index].hash);
    return hashes;
}
const Commit* HistoryView::CommitFor(const std::string& hash) const {
    for (const auto& c : fCommits) if (c.hash == hash) return &c;
    return nullptr;
}
void HistoryView::SelectHash(const std::string& hash, bool notify) {
    for (size_t i = 0; i < fCommits.size(); ++i) if (fCommits[i].hash == hash) {
        fSelected.clear(); fSelected.insert(static_cast<int>(i)); fAnchor = fLead = static_cast<int>(i);
        for (size_t v = 0; v < fVisible.size(); ++v) if (fVisible[v] == static_cast<int>(i)) EnsureVisible(static_cast<int>(v));
        Invalidate();
        if (notify) NotifySelection();
        return;
    }
}
void HistoryView::EnsureVisible(int row) {
    BScrollBar* bar = ScrollBar(B_VERTICAL);
    if (!bar) return;
    float top = row * fRowHeight, bottom = top + fRowHeight;
    float min, max; bar->GetRange(&min, &max);
    if (top < bar->Value()) bar->SetValue(std::max(min, top));
    else if (bottom > bar->Value() + Bounds().Height()) bar->SetValue(std::min(max, bottom - Bounds().Height()));
}
void HistoryView::Draw(BRect updateRect) {
    rgb_color base = Palette::Current().background;
    rgb_color text = Palette::Current().text;
    SetHighColor(base); FillRect(updateRect);
    font_height fh; PlainFont().GetHeight(&fh);
    if (fVisible.empty() && !fLoading) {
        SetFont(&PlainFont()); SetHighColor(fPalette.muted);
        DrawString(fCommits.empty() ? "No commits yet." : "No commits match the search.", BPoint(16, 24 + fh.ascent));
        return;
    }
    int first = std::max(0, static_cast<int>(std::floor(updateRect.top / fRowHeight)));
    int last = std::min(static_cast<int>(fVisible.size()), static_cast<int>(std::floor(updateRect.bottom / fRowHeight)));
    const float width = Bounds().Width();
    auto rowFrame = [&](int row) { return BRect(0, row * fRowHeight, width, (row + 1) * fRowHeight - 1); };
    auto rowColor = [&](int row, int index) {
        return fSelected.count(index) ? Palette::Current().selection : (row % 2 ? Mix(base, Palette::Current().panel, 0.18f) : base);
    };
    // Pass 1: every row background, so no row paints over its neighbour's lines.
    for (int row = first; row <= last && row < static_cast<int>(fVisible.size()); ++row) {
        SetHighColor(rowColor(row, fVisible[row])); FillRect(rowFrame(row));
    }
    // Pass 2: the graph for the rows in view (one row beyond each edge keeps links whole).
    if (fFilter.empty()) {
        PushState();
        ClipToRect(BRect(0, updateRect.top, fGraphWidth - 1, updateRect.bottom));
        SetDrawingMode(B_OP_ALPHA);
        SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
        int from = std::max(0, first - 1), to = std::min(static_cast<int>(fVisible.size()) - 1, last + 1);
        for (int pass = 0; pass < 3; ++pass)
            for (int row = from; row <= to; ++row) {
                int index = fVisible[row];
                if (index < static_cast<int>(fRows.size())) DrawGraph(fRows[index], row * fRowHeight, pass, fSelected.count(index) > 0, rowColor(row, index));
            }
        PopState();
        // Rows wider than the column fade out at its edge.
        for (int row = first; row <= last && row < static_cast<int>(fVisible.size()); ++row) {
            int index = fVisible[row];
            if (index >= static_cast<int>(fRows.size()) || kGraphInset + fRows[index].lanes * kLaneWidth <= fGraphWidth) continue;
            rgb_color back = rowColor(row, index);
            BRect frame = rowFrame(row);
            SetDrawingMode(B_OP_ALPHA);
            for (int i = 0; i < 10; ++i) {
                back.alpha = static_cast<uint8>(25 * (i + 1));
                SetHighColor(back);
                FillRect(BRect(fGraphWidth - 10 + i, frame.top, fGraphWidth - 10 + i, frame.bottom));
            }
            SetDrawingMode(B_OP_COPY);
        }
    }
    // Pass 3: badges and text.
    for (int row = first; row <= last; ++row) {
        BRect frame = rowFrame(row);
        if (row == static_cast<int>(fVisible.size())) {
            SetFont(&PlainFont()); SetHighColor(fPalette.muted);
            DrawString(fLoading ? "Loading" B_UTF8_ELLIPSIS : "Scroll to load older commits", BPoint(fGraphWidth + 8, frame.top + 4 + fh.ascent));
            if (fMore && !fLoading && !fMoreRequested) { fMoreRequested = true; fTarget.SendMessage(kHistoryMore); }
            continue;
        }
        int index = fVisible[row];
        const Commit& commit = fCommits[index];
        bool selected = fSelected.count(index) > 0;
        rgb_color background = rowColor(row, index);
        float baseline = frame.top + (fRowHeight - (fh.ascent + fh.descent)) / 2 + fh.ascent;
        float x = fGraphWidth + 6;
        rgb_color textColor = selected ? Palette::Current().selectionText : text;
        // Ref badges: branches, remote branches, tags, HEAD.
        for (const auto& ref : commit.refs) {
            std::string label = ref; rgb_color fill = fPalette.badge;
            if (label.rfind("HEAD -> ", 0) == 0) { label = label.substr(8); fill = fPalette.current; }
            else if (label == "HEAD") fill = fPalette.current;
            else if (label.rfind("tag: ", 0) == 0) { label = label.substr(5); fill = fPalette.tag; }
            else if (label.find('/') != std::string::npos) fill = fPalette.remote;
            else if (label.rfind("refs/stash", 0) == 0) { label = "stash"; fill = fPalette.stash; }
            if (label.rfind("refs/", 0) == 0) continue;
            DrawBadge(Elide(label, 28), fill, x, baseline, frame.top + 3, frame.bottom - 3);
            if (x > width - kAuthorWidth - kDateWidth - 60) break;
        }
        SetFont(commit.hash == fHead ? &BoldFont() : &PlainFont());
        SetHighColor(textColor);
        float subjectRight = width - kAuthorWidth - kDateWidth - 12;
        BFont font; GetFont(&font);
        BString truncated(commit.subject.c_str());
        font.TruncateString(&truncated, B_TRUNCATE_END, std::max(20.0f, subjectRight - x));
        DrawString(truncated.String(), BPoint(x, baseline));
        SetFont(&PlainFont());
        SetHighColor(selected ? textColor : Mix(textColor, background, 0.3f));
        BString author(commit.authorName.c_str());
        PlainFont().TruncateString(&author, B_TRUNCATE_END, kAuthorWidth - 8);
        DrawString(author.String(), BPoint(width - kAuthorWidth - kDateWidth, baseline));
        std::string date = FormatDate(commit.authorDate, true);
        DrawString(date.c_str(), BPoint(width - kDateWidth, baseline));
    }
}
void HistoryView::DrawBadge(const std::string& label, rgb_color fill, float& x, float baseline, float top, float bottom) {
    BFont small(&PlainFont()); small.SetSize(std::max(8.0f, PlainFont().Size() * 0.85f));
    SetFont(&small);
    float w = small.StringWidth(label.c_str()) + 10;
    BRect badge(x, top, x + w, bottom);
    SetHighColor(fill); FillRoundRect(badge, 4, 4);
    SetHighColor(Mix(fill, Palette::Current().text, 0.45f)); StrokeRoundRect(badge, 4, 4);
    SetHighColor(DarkUI() ? rgb_color{240, 240, 240, 255} : rgb_color{30, 30, 30, 255});
    DrawString(label.c_str(), BPoint(x + 5, baseline - 1));
    x += w + 5;
    SetFont(&PlainFont());
}
void HistoryView::DrawGraph(const GraphRow& row, float top, int pass, bool selected, rgb_color background) {
    // Pass 0: straight rails. Pass 1: links, which leave or enter the node
    // horizontally and turn into their column with a rounded corner. Pass 2: nodes.
    const float bottom = top + fRowHeight;
    const float cy = std::floor(top + fRowHeight / 2);
    auto laneX = [&](int lane) { return std::floor(kGraphInset + lane * kLaneWidth); };
    const float nodeX = laneX(row.lane);
    if (pass == 0) {
        SetPenSize(2);
        for (const auto& e : row.up) {
            if (e.from != e.to) continue;
            SetHighColor(LaneColor(e.color));
            StrokeLine(BPoint(laneX(e.from), top), BPoint(laneX(e.from), e.from == row.lane ? cy : bottom));
        }
        for (const auto& e : row.down) {
            if (e.from != e.to) continue;
            SetHighColor(LaneColor(e.color));
            // A pass-through was drawn full height by its up edge already.
            bool passThrough = e.from != row.lane;
            if (passThrough) {
                bool drawn = false;
                for (const auto& u : row.up) if (u.from == e.from && u.to == e.to) drawn = true;
                if (drawn) continue;
            }
            StrokeLine(BPoint(laneX(e.from), passThrough ? top : cy), BPoint(laneX(e.from), bottom));
        }
        return;
    }
    if (pass == 1) {
        SetPenSize(2);
        const float radius = std::min(fRowHeight / 2 - kLinkOffset - 1, kLaneWidth * 0.6f);
        auto corner = [&](BPoint start, BPoint control, BPoint end) {
            BPoint points[4] = {start, BPoint(start.x + (control.x - start.x) * 0.55f, start.y + (control.y - start.y) * 0.55f),
                BPoint(end.x + (control.x - end.x) * 0.55f, end.y + (control.y - end.y) * 0.55f), end};
            StrokeBezier(points);
        };
        for (const auto& e : row.up) {
            if (e.from == e.to) continue;
            // Converging lane: down its column, round the corner, across to the node.
            // Runs just above the centre so it never overlaps a link leaving the node.
            float x = laneX(e.from), side = x > nodeX ? 1 : -1, y = cy - kLinkOffset;
            float r = std::min(radius, std::fabs(x - nodeX));
            SetHighColor(LaneColor(e.color));
            StrokeLine(BPoint(x, top), BPoint(x, y - r));
            corner(BPoint(x, y - r), BPoint(x, y), BPoint(x - side * r, y));
            StrokeLine(BPoint(x - side * r, y), BPoint(nodeX, y));
        }
        for (const auto& e : row.down) {
            if (e.from == e.to) continue;
            // Parent link: across from the node, round the corner, down its column.
            // Runs just below the centre, clear of lanes converging from above.
            float x = laneX(e.to), side = x > nodeX ? 1 : -1, y = cy + kLinkOffset;
            float r = std::min(radius, std::fabs(x - nodeX));
            SetHighColor(LaneColor(e.color));
            StrokeLine(BPoint(nodeX, y), BPoint(x - side * r, y));
            corner(BPoint(x - side * r, y), BPoint(x, y), BPoint(x, y + r));
            StrokeLine(BPoint(x, y + r), BPoint(x, bottom));
        }
        return;
    }
    SetPenSize(1);
    BRect node(nodeX - kNodeRadius, cy - kNodeRadius, nodeX + kNodeRadius, cy + kNodeRadius);
    rgb_color fill = LaneColor(row.color);
    SetHighColor(fill); FillEllipse(node);
    if (row.merge) { SetHighColor(background); FillEllipse(node.InsetByCopy(2.5f, 2.5f)); }
    if (selected) { SetHighColor(Palette::Current().selectionText); StrokeEllipse(node.InsetByCopy(-2, -2)); }
}
void HistoryView::NotifySelection() {
    BMessage message(kHistorySelection);
    for (const auto& hash : SelectedHashes()) message.AddString("hash", hash.c_str());
    fTarget.SendMessage(&message);
}
void HistoryView::MouseDown(BPoint where) {
    MakeFocus(true);
    int32 buttons = 0, clicks = 1;
    if (Window() && Window()->CurrentMessage()) { Window()->CurrentMessage()->FindInt32("buttons", &buttons); Window()->CurrentMessage()->FindInt32("clicks", &clicks); }
    int row = RowAt(where);
    if (row < 0) { if (!(buttons & B_SECONDARY_MOUSE_BUTTON)) { fSelected.clear(); Invalidate(); NotifySelection(); } return; }
    int index = fVisible[row];
    if (buttons & B_SECONDARY_MOUSE_BUTTON) {
        if (!fSelected.count(index)) { fSelected.clear(); fSelected.insert(index); fAnchor = fLead = index; Invalidate(); NotifySelection(); }
        ShowContextMenu(ConvertToScreen(where), index);
        return;
    }
    if (modifiers() & B_SHIFT_KEY && fAnchor >= 0) {
        fSelected.clear();
        int from = std::min(fAnchor, index), to = std::max(fAnchor, index);
        for (int i = from; i <= to; ++i) fSelected.insert(i);
        fLead = index;
    } else if (modifiers() & B_COMMAND_KEY) {
        if (fSelected.count(index)) fSelected.erase(index); else fSelected.insert(index);
        fAnchor = fLead = index;
    } else {
        if (!fSelected.count(index)) { fSelected.clear(); fSelected.insert(index); }
        fAnchor = fLead = index;
        fPressed = where; fDragArmed = true; fPressedIndex = index;
        SetMouseEventMask(B_POINTER_EVENTS, B_NO_POINTER_HISTORY | B_LOCK_WINDOW_FOCUS);
    }
    Invalidate();
    NotifySelection();
    if (clicks == 2 && !fDragArmed) { BMessage invoke(kCheckoutCommit); invoke.AddString("hash", fCommits[index].hash.c_str()); fTarget.SendMessage(&invoke); }
}
void HistoryView::MouseUp(BPoint where) {
    if (fDragArmed) {
        // A plain click on an already selected commit narrows the selection to it.
        int row = RowAt(where);
        if (row >= 0 && fVisible[row] == fPressedIndex && fSelected.size() > 1 && !(modifiers() & (B_SHIFT_KEY | B_COMMAND_KEY))) {
            fSelected.clear(); fSelected.insert(fPressedIndex); Invalidate(); NotifySelection();
        }
    }
    fDragArmed = false;
}
void HistoryView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag) {
    if (fDragArmed && (std::fabs(where.x - fPressed.x) > 4 || std::fabs(where.y - fPressed.y) > 4)) { fDragArmed = false; StartDrag(where); }
    BView::MouseMoved(where, transit, drag);
}
void HistoryView::StartDrag(BPoint where) {
    auto hashes = SelectedHashes();
    if (hashes.empty()) return;
    BMessage drag(kDragCommit);
    for (const auto& h : hashes) drag.AddString("hash", h.c_str());
    const Commit* commit = CommitFor(hashes.front());
    std::string label = hashes.size() == 1 ? (commit ? commit->shortHash() + "  " + Elide(commit->subject, 40) : hashes.front().substr(0, 8)) : std::to_string(hashes.size()) + " commits";
    drag.AddString("label", label.c_str());
    font_height fh; PlainFont().GetHeight(&fh);
    float w = PlainFont().StringWidth(label.c_str()) + 20, h = fh.ascent + fh.descent + 10;
    BBitmap* bitmap = new BBitmap(BRect(0, 0, w, h), B_RGBA32, true);
    BView* painter = new BView(bitmap->Bounds(), "drag", B_FOLLOW_NONE, 0);
    bitmap->AddChild(painter);
    bitmap->Lock();
    painter->SetHighColor(fPalette.badge); painter->SetLowColor(fPalette.badge);
    painter->FillRoundRect(bitmap->Bounds(), 5, 5);
    painter->SetHighColor(fPalette.badgeText);
    painter->DrawString(label.c_str(), BPoint(10, 5 + fh.ascent));
    painter->Sync();
    bitmap->Unlock();
    bitmap->RemoveChild(painter); delete painter;
    DragMessage(&drag, bitmap, B_OP_ALPHA, BPoint(w / 2, h / 2));
}
void HistoryView::ShowContextMenu(BPoint screen, int index) {
    const Commit& commit = fCommits[index];
    bool single = fSelected.size() == 1;
    std::vector<MenuChoice> choices = {
        {"Checkout This Commit", kCheckoutCommit, single},
        {"Create Branch Here" B_UTF8_ELLIPSIS, kBranchHere, single},
        {"Create Tag Here" B_UTF8_ELLIPSIS, kTagHere, single},
        {single ? "Cherry-pick Into Current Branch" : "Cherry-pick Selected Commits Into Current Branch", kCherryPick, true, true},
        {"Revert This Commit", kRevertCommit, single},
        {"Reset Current Branch to Here (Soft: keep changes staged)", kResetSoft, single, true},
        {"Reset Current Branch to Here (Mixed: keep changes unstaged)", kResetMixed, single},
        {"Reset Current Branch to Here (Hard: discard changes)", kResetHard, single},
        {"Compare Selected Commits", kCompareCommits, fSelected.size() == 2, true},
        {"Copy Commit Hash", kCopyHash, single, true},
    };
    uint32 command = PopupChoice(this, screen, choices);
    if (!command) return;
    BMessage message(command);
    for (const auto& h : SelectedHashes()) message.AddString("hash", h.c_str());
    if (single) message.AddString("hash", commit.hash.c_str());
    fTarget.SendMessage(&message);
}
void HistoryView::KeyDown(const char* bytes, int32 count) {
    if (count != 1) { BView::KeyDown(bytes, count); return; }
    int row = -1;
    for (size_t v = 0; v < fVisible.size(); ++v) if (fVisible[v] == fLead) row = static_cast<int>(v);
    switch (bytes[0]) {
        case B_DOWN_ARROW: row = std::min(static_cast<int>(fVisible.size()) - 1, row + 1); break;
        case B_UP_ARROW: row = std::max(0, row - 1); break;
        case B_PAGE_DOWN: row = std::min(static_cast<int>(fVisible.size()) - 1, row + static_cast<int>(Bounds().Height() / fRowHeight)); break;
        case B_PAGE_UP: row = std::max(0, row - static_cast<int>(Bounds().Height() / fRowHeight)); break;
        case B_HOME: row = 0; break;
        case B_END: row = static_cast<int>(fVisible.size()) - 1; break;
        case B_ENTER: { if (fLead >= 0) { BMessage invoke(kCheckoutCommit); invoke.AddString("hash", fCommits[fLead].hash.c_str()); fTarget.SendMessage(&invoke); } return; }
        default: BView::KeyDown(bytes, count); return;
    }
    if (row < 0 || row >= static_cast<int>(fVisible.size())) return;
    int index = fVisible[row];
    if (modifiers() & B_SHIFT_KEY && fAnchor >= 0) { fSelected.clear(); for (int i = std::min(fAnchor, index); i <= std::max(fAnchor, index); ++i) fSelected.insert(i); }
    else { fSelected.clear(); fSelected.insert(index); fAnchor = index; }
    fLead = index;
    EnsureVisible(row);
    Invalidate();
    NotifySelection();
}
void HistoryView::MessageReceived(BMessage* message) {
    switch (message->what) {
        case B_MOUSE_WHEEL_CHANGED: {
            float dy = 0; message->FindFloat("be:wheel_delta_y", &dy);
            if (BScrollBar* bar = ScrollBar(B_VERTICAL)) { float min, max; bar->GetRange(&min, &max); bar->SetValue(std::max(min, std::min(max, bar->Value() + dy * fRowHeight * 3))); }
            break;
        }
        default: BView::MessageReceived(message);
    }
}
}
