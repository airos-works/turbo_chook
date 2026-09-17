#include "ui/DiffView.h"
#include "ui/Messages.h"
#include <ScrollBar.h>
#include <Window.h>
#include <algorithm>
#include <cmath>

namespace tc {
DiffView::DiffView()
    : BView("diff", B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE), fPalette(Palette::Current()) {
    fFont = *be_fixed_font;
    fBoldFont = *be_bold_font;
    SetViewColor(B_TRANSPARENT_COLOR);
    SetLowColor(ui_color(B_LIST_BACKGROUND_COLOR));
}
void DiffView::AttachedToWindow() {
    BView::AttachedToWindow();
    fPalette = Palette::Current();
    font_height fh; fFont.GetHeight(&fh);
    fRowHeight = std::ceil(fh.ascent + fh.descent + fh.leading) + 4;
    fCharWidth = fFont.StringWidth("0");
    fNumberWidth = fCharWidth * 5 + 8;
    Rebuild();
}
void DiffView::SetDiff(const std::string& unified, Mode mode, const std::string& title) {
    // The same diff again (a refresh) keeps the scroll position and line selection.
    bool same = unified == fUnified && mode == fMode && title == fTitle && !fFiles.empty();
    fUnified = unified;
    fFiles = ParseUnifiedDiff(unified);
    fMode = mode; fTitle = title; fMessage.clear();
    if (fFiles.empty()) fMessage = unified.empty() ? "No changes." : "No textual changes to show.";
    if (!same) { fSelectedRows.clear(); fSelectedHunk = -1; fSelectedFile = -1; fSelectionAnchor = -1; }
    Rebuild();
    if (!same) ScrollTo(BPoint(0, 0));
}
void DiffView::ScrollTo(BPoint where) {
    BPoint before = Bounds().LeftTop();
    BView::ScrollTo(where);
    // Horizontal scrolling moves the content but the gutter, headers and buttons stay put,
    // so the copied pixels are wrong; repaint everything instead.
    if (before.x != Bounds().left) Invalidate();
}
void DiffView::SetMessage(const std::string& message) {
    fFiles.clear(); fMessage = message; fTitle.clear(); fUnified.clear();
    fSelectedRows.clear(); fSelectedHunk = -1; fSelectedFile = -1;
    Rebuild();
    ScrollTo(BPoint(0, 0));
}
void DiffView::Rebuild() {
    fRows.clear();
    size_t longest = 0;
    for (size_t f = 0; f < fFiles.size(); ++f) {
        fRows.push_back({Row::kFileHeader, static_cast<int>(f), -1, -1});
        const auto& file = fFiles[f];
        if (file.binary) fRows.push_back({Row::kNote, static_cast<int>(f), -1, -1});
        for (size_t h = 0; h < file.hunks.size(); ++h) {
            fRows.push_back({Row::kHunkHeader, static_cast<int>(f), static_cast<int>(h), -1});
            for (size_t l = 0; l < file.hunks[h].lines.size(); ++l) {
                fRows.push_back({Row::kLine, static_cast<int>(f), static_cast<int>(h), static_cast<int>(l)});
                longest = std::max(longest, file.hunks[h].lines[l].text.size());
            }
        }
    }
    fContentWidth = fNumberWidth * 2 + 24 + fCharWidth * (longest + 2) * 1.0f;
    UpdateScrollBars();
    Invalidate();
}
void DiffView::UpdateScrollBars() {
    float height = fRows.size() * fRowHeight + 8;
    float width = std::max(fContentWidth, Bounds().Width());
    if (BScrollBar* bar = ScrollBar(B_VERTICAL)) {
        float range = std::max(0.0f, height - Bounds().Height());
        bar->SetRange(0, range);
        bar->SetSteps(fRowHeight, Bounds().Height() - fRowHeight);
        bar->SetProportion(height > 0 ? std::min(1.0f, Bounds().Height() / height) : 1.0f);
    }
    if (BScrollBar* bar = ScrollBar(B_HORIZONTAL)) {
        float range = std::max(0.0f, width - Bounds().Width());
        bar->SetRange(0, range);
        bar->SetSteps(fCharWidth * 4, Bounds().Width() - fCharWidth * 4);
        bar->SetProportion(width > 0 ? std::min(1.0f, Bounds().Width() / width) : 1.0f);
    }
}
void DiffView::FrameResized(float, float) { UpdateScrollBars(); }
int DiffView::RowAt(BPoint where) const {
    int index = static_cast<int>(std::floor(where.y / fRowHeight));
    return index >= 0 && index < static_cast<int>(fRows.size()) ? index : -1;
}
void DiffView::Draw(BRect updateRect) {
    rgb_color base = ui_color(B_LIST_BACKGROUND_COLOR);
    SetHighColor(base); FillRect(updateRect);
    if (fRows.empty()) {
        SetFont(be_plain_font);
        SetHighColor(fPalette.muted);
        font_height fh; be_plain_font->GetHeight(&fh);
        std::string message = fMessage.empty() ? "Select a change to see its diff." : fMessage;
        size_t start = 0;
        float y = Bounds().top + 24 + fh.ascent;
        while (start <= message.size()) {
            auto end = message.find('\n', start);
            if (end == std::string::npos) end = message.size();
            DrawString(message.substr(start, end - start).c_str(), BPoint(Bounds().left + 16, y));
            y += fh.ascent + fh.descent + 4;
            start = end + 1;
        }
        return;
    }
    int first = std::max(0, static_cast<int>(std::floor(updateRect.top / fRowHeight)));
    int last = std::min(static_cast<int>(fRows.size()) - 1, static_cast<int>(std::floor(updateRect.bottom / fRowHeight)));
    float width = std::max(fContentWidth, Bounds().Width());
    for (int i = first; i <= last; ++i) {
        BRect frame(0, i * fRowHeight, width, (i + 1) * fRowHeight - 1);
        DrawRow(fRows[i], frame, i);
    }
}
void DiffView::DrawRow(const Row& row, BRect frame, int index) {
    font_height fh; fFont.GetHeight(&fh);
    const float baseline = frame.top + 2 + fh.ascent;
    const float left = Bounds().left;   // headers stay put horizontally
    rgb_color text = ui_color(B_LIST_ITEM_TEXT_COLOR);
    const DiffFile& file = fFiles[row.file];
    if (row.kind == Row::kFileHeader) {
        SetHighColor(fPalette.header); FillRect(frame);
        SetHighColor(Mix(fPalette.header, text, 0.25f)); StrokeLine(frame.LeftBottom(), frame.RightBottom());
        SetFont(&fBoldFont); SetHighColor(text);
        std::string label = file.displayPath();
        if (file.renamed && !file.oldPath.empty()) label = file.oldPath + " \xe2\x86\x92 " + file.newPath;
        if (file.added) label += "  (new file)"; else if (file.deleted) label += "  (deleted)";
        DrawString(label.c_str(), BPoint(left + 8, baseline));
        int added = 0, removed = 0;
        for (const auto& h : file.hunks) { added += h.added(); removed += h.removed(); }
        SetFont(&fFont);
        std::string counts = "+" + std::to_string(added) + "  -" + std::to_string(removed);
        float w = fFont.StringWidth(counts.c_str());
        SetHighColor(fPalette.addedText); DrawString(("+" + std::to_string(added)).c_str(), BPoint(left + Bounds().Width() - w - 12, baseline));
        SetHighColor(fPalette.removedText); DrawString(("-" + std::to_string(removed)).c_str(), BPoint(left + Bounds().Width() - fFont.StringWidth(("-" + std::to_string(removed)).c_str()) - 12, baseline));
        return;
    }
    if (row.kind == Row::kNote) {
        SetHighColor(ui_color(B_LIST_BACKGROUND_COLOR)); FillRect(frame);
        SetFont(be_plain_font); SetHighColor(fPalette.muted);
        DrawString("Binary file; no textual diff.", BPoint(left + 12, baseline));
        return;
    }
    const DiffHunk& hunk = file.hunks[row.hunk];
    if (row.kind == Row::kHunkHeader) {
        SetHighColor(fPalette.hunk); FillRect(frame);
        SetFont(&fFont); SetHighColor(Mix(text, fPalette.hunk, 0.25f));
        DrawString(Elide(hunk.header, 90).c_str(), BPoint(left + 8, baseline));
        // Action buttons on the right edge.
        SetFont(be_plain_font);
        font_height ph; be_plain_font->GetHeight(&ph);
        for (const auto& button : ButtonsFor(index)) {
            SetHighColor(Mix(fPalette.hunk, ui_color(B_CONTROL_BACKGROUND_COLOR), 0.8f)); FillRoundRect(button.frame, 3, 3);
            SetHighColor(Mix(fPalette.hunk, text, 0.4f)); StrokeRoundRect(button.frame, 3, 3);
            SetHighColor(text);
            DrawString(button.label.c_str(), BPoint(button.frame.left + 8, button.frame.top + (button.frame.Height() - (ph.ascent + ph.descent)) / 2 + ph.ascent));
        }
        return;
    }
    const DiffLine& line = hunk.lines[row.line];
    rgb_color background = ui_color(B_LIST_BACKGROUND_COLOR);
    rgb_color numbers = fPalette.muted;
    if (line.kind == '+') background = fPalette.added;
    else if (line.kind == '-') background = fPalette.removed;
    else if (line.kind == '\\') background = Mix(background, fPalette.muted, 0.1f);
    bool selected = fSelectedRows.count(index) > 0;
    if (selected) background = Mix(background, ui_color(B_LIST_SELECTED_BACKGROUND_COLOR), 0.55f);
    SetHighColor(background); FillRect(frame);
    SetFont(&fFont);
    // The text lives in content coordinates so it scrolls; the gutter is painted
    // over it afterwards at the visible left edge so the numbers stay put.
    const float gutterWidth = fNumberWidth * 2 + 12;
    std::string content;
    content.reserve(line.text.size() + 8);
    size_t column = 0;
    for (char c : line.text) {
        if (c == '\t') { size_t pad = 4 - column % 4; content.append(pad, ' '); column += pad; }
        else { content += c; ++column; }
    }
    if (line.kind == '\\') content = "(no newline at end of file)";
    SetHighColor(line.kind == '+' ? fPalette.addedText : line.kind == '-' ? fPalette.removedText : text);
    std::string marker(1, line.kind == '\\' ? ' ' : line.kind);
    DrawString(marker.c_str(), BPoint(gutterWidth + 4, baseline));
    DrawString(content.c_str(), BPoint(gutterWidth + 4 + fCharWidth * 2, baseline));
    BRect gutter(left, frame.top, left + gutterWidth, frame.bottom);
    SetHighColor(Mix(background, ui_color(B_PANEL_BACKGROUND_COLOR), 0.35f)); FillRect(gutter);
    SetHighColor(numbers);
    if (line.oldNumber > 0) { std::string n = std::to_string(line.oldNumber); DrawString(n.c_str(), BPoint(gutter.left + fNumberWidth - fFont.StringWidth(n.c_str()) - 4, baseline)); }
    if (line.newNumber > 0) { std::string n = std::to_string(line.newNumber); DrawString(n.c_str(), BPoint(gutter.left + fNumberWidth * 2 - fFont.StringWidth(n.c_str()) - 4, baseline)); }
}
std::vector<DiffView::Button> DiffView::ButtonsFor(int index) const {
    std::vector<Button> result;
    if (index < 0 || index >= static_cast<int>(fRows.size()) || fRows[index].kind != Row::kHunkHeader) return result;
    const Row& row = fRows[index];
    bool linesSelected = fSelectedFile == row.file && fSelectedHunk == row.hunk && !fSelectedRows.empty();
    std::string suffix = linesSelected ? " Lines" : " Hunk";
    std::vector<std::pair<std::string, uint32>> actions;
    if (fMode == kUnstaged) { actions.push_back({"Stage" + suffix, linesSelected ? kStageLines : kStageHunk}); actions.push_back({"Discard" + suffix, linesSelected ? kDiscardLines : kDiscardHunk}); }
    else if (fMode == kStaged) actions.push_back({"Unstage" + suffix, linesSelected ? kUnstageLines : kUnstageHunk});
    float top = index * fRowHeight, bottom = (index + 1) * fRowHeight - 1;
    float x = Bounds().left + Bounds().Width() - 8;
    for (auto it = actions.rbegin(); it != actions.rend(); ++it) {
        float w = be_plain_font->StringWidth(it->first.c_str()) + 16;
        result.push_back({BRect(x - w, top + 1, x, bottom - 1), it->second, row.file, row.hunk, it->first});
        x -= w + 6;
    }
    return result;
}
void DiffView::MouseDown(BPoint where) {
    MakeFocus(true);
    int32 buttons = 0;
    if (Window() && Window()->CurrentMessage()) Window()->CurrentMessage()->FindInt32("buttons", &buttons);
    int index = RowAt(where);
    if (index < 0) return;
    for (const auto& button : ButtonsFor(index)) {
        if (button.frame.Contains(where)) {
            bool lines = button.command == kStageLines || button.command == kUnstageLines || button.command == kDiscardLines;
            SendHunkAction(button.command, button.file, button.hunk, lines);
            return;
        }
    }
    const Row& row = fRows[index];
    if (row.kind != Row::kLine || fMode == kReadOnly || fMode == kUntracked) return;
    const DiffLine& line = fFiles[row.file].hunks[row.hunk].lines[row.line];
    if (line.kind == ' ' || line.kind == '\\') { if (!(modifiers() & B_SHIFT_KEY)) { fSelectedRows.clear(); fSelectedHunk = -1; Invalidate(); } return; }
    if (buttons & B_SECONDARY_MOUSE_BUTTON) {
        std::vector<MenuChoice> choices;
        if (fMode == kUnstaged) choices = {{"Stage Hunk", kStageHunk}, {"Discard Hunk", kDiscardHunk}};
        else choices = {{"Unstage Hunk", kUnstageHunk}};
        uint32 command = PopupChoice(this, ConvertToScreen(where), choices);
        if (command) SendHunkAction(command, row.file, row.hunk, false);
        return;
    }
    if (fSelectedHunk != row.hunk || fSelectedFile != row.file) { fSelectedRows.clear(); fSelectionAnchor = -1; }
    fSelectedHunk = row.hunk; fSelectedFile = row.file;
    if ((modifiers() & B_SHIFT_KEY) && fSelectionAnchor >= 0) {
        int from = std::min(fSelectionAnchor, index), to = std::max(fSelectionAnchor, index);
        for (int i = from; i <= to; ++i) if (fRows[i].kind == Row::kLine && fRows[i].hunk == row.hunk && fRows[i].file == row.file) {
            const auto& l = fFiles[row.file].hunks[row.hunk].lines[fRows[i].line];
            if (l.kind == '+' || l.kind == '-') fSelectedRows.insert(i);
        }
    } else if (modifiers() & B_COMMAND_KEY) {
        if (fSelectedRows.count(index)) fSelectedRows.erase(index); else fSelectedRows.insert(index);
        fSelectionAnchor = index;
    } else {
        if (fSelectedRows.size() == 1 && fSelectedRows.count(index)) fSelectedRows.clear();
        else { fSelectedRows.clear(); fSelectedRows.insert(index); }
        fSelectionAnchor = index;
    }
    Invalidate();
}
void DiffView::MouseMoved(BPoint where, uint32 transit, const BMessage* drag) {
    int index = RowAt(where);
    if (index != fHoverRow) { fHoverRow = index; }
    BView::MouseMoved(where, transit, drag);
}
void DiffView::KeyDown(const char* bytes, int32 count) {
    if (count == 1 && (bytes[0] == B_PAGE_DOWN || bytes[0] == B_PAGE_UP || bytes[0] == B_DOWN_ARROW || bytes[0] == B_UP_ARROW || bytes[0] == B_HOME || bytes[0] == B_END)) {
        BScrollBar* bar = ScrollBar(B_VERTICAL);
        if (!bar) return;
        float min, max; bar->GetRange(&min, &max);
        float value = bar->Value();
        switch (bytes[0]) {
            case B_PAGE_DOWN: value += Bounds().Height() - fRowHeight; break;
            case B_PAGE_UP: value -= Bounds().Height() - fRowHeight; break;
            case B_DOWN_ARROW: value += fRowHeight; break;
            case B_UP_ARROW: value -= fRowHeight; break;
            case B_HOME: value = min; break;
            case B_END: value = max; break;
        }
        bar->SetValue(std::max(min, std::min(max, value)));
        return;
    }
    if (count == 1 && bytes[0] == B_ESCAPE) { fSelectedRows.clear(); fSelectedHunk = -1; Invalidate(); return; }
    BView::KeyDown(bytes, count);
}
void DiffView::MessageReceived(BMessage* message) {
    switch (message->what) {
        case B_MOUSE_WHEEL_CHANGED: {
            float dy = 0, dx = 0;
            message->FindFloat("be:wheel_delta_y", &dy); message->FindFloat("be:wheel_delta_x", &dx);
            if (BScrollBar* bar = ScrollBar(B_VERTICAL)) { float min, max; bar->GetRange(&min, &max); bar->SetValue(std::max(min, std::min(max, bar->Value() + dy * fRowHeight * 3))); }
            if (BScrollBar* bar = ScrollBar(B_HORIZONTAL)) { float min, max; bar->GetRange(&min, &max); bar->SetValue(std::max(min, std::min(max, bar->Value() + dx * fCharWidth * 8))); }
            break;
        }
        default: BView::MessageReceived(message);
    }
}
void DiffView::ScrollToHunk(int direction) {
    BScrollBar* bar = ScrollBar(B_VERTICAL);
    if (!bar || fRows.empty()) return;
    int current = static_cast<int>(bar->Value() / fRowHeight);
    int target = -1;
    if (direction > 0) { for (int i = current + 1; i < static_cast<int>(fRows.size()); ++i) if (fRows[i].kind == Row::kHunkHeader) { target = i; break; } }
    else { for (int i = current - 1; i >= 0; --i) if (fRows[i].kind == Row::kHunkHeader) { target = i; break; } }
    if (target < 0) return;
    float min, max; bar->GetRange(&min, &max);
    bar->SetValue(std::max(min, std::min(max, target * fRowHeight)));
}
void DiffView::SendHunkAction(uint32 command, int file, int hunk, bool lines) {
    if (file < 0 || file >= static_cast<int>(fFiles.size()) || hunk < 0 || hunk >= static_cast<int>(fFiles[file].hunks.size())) return;
    std::vector<size_t> selected;
    if (lines) for (int index : fSelectedRows) if (fRows[index].file == file && fRows[index].hunk == hunk) selected.push_back(static_cast<size_t>(fRows[index].line));
    std::string patch = HunkPatch(fFiles[file], fFiles[file].hunks[hunk], selected);
    BMessage message(command);
    message.AddString("patch", patch.c_str());
    message.AddString("path", fFiles[file].displayPath().c_str());
    message.AddInt32("hunk", hunk);
    fTarget.SendMessage(&message);
    fSelectedRows.clear(); fSelectedHunk = -1;
}
}
