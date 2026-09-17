#include "ui/WorkingCopyView.h"
#include "ui/DiffView.h"
#include "ui/Messages.h"
#include "ui/Widgets.h"
#include <Button.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <ScrollView.h>
#include <SplitView.h>
#include <StringView.h>
#include <TextView.h>
#include <Window.h>
#include <algorithm>
#include <cmath>

namespace tc {
// ---------------------------------------------------------------- FileItem
void FileItem::Update(BView* owner, const BFont* font) {
    BListItem::Update(owner, font);
    font_height fh; font->GetHeight(&fh);
    SetHeight(std::ceil(fh.ascent + fh.descent) + 8);
}
void FileItem::DrawItem(BView* owner, BRect frame, bool) {
    Palette palette = Palette::Current();
    rgb_color base = IsSelected() ? Palette::Current().selection : Palette::Current().background;
    rgb_color text = IsSelected() ? Palette::Current().selectionText : Palette::Current().text;
    owner->SetHighColor(base); owner->FillRect(frame);
    font_height fh; PlainFont().GetHeight(&fh);
    float baseline = frame.top + (frame.Height() - (fh.ascent + fh.descent)) / 2 + fh.ascent;
    char code = fFile.conflicted() ? '!' : fFile.untracked() ? '?' : fStagedSide ? fFile.index : fFile.worktree;
    rgb_color badge = palette.badge;
    switch (code) {
        case 'M': case 'T': badge = palette.tag; break;
        case 'A': case '?': badge = palette.current; break;
        case 'D': badge = palette.removed; break;
        case 'R': case 'C': badge = palette.stash; break;
        case '!': badge = palette.conflict; break;
        default: break;
    }
    BRect box(frame.left + 6, frame.top + 3, frame.left + 22, frame.bottom - 3);
    owner->SetHighColor(badge); owner->FillRoundRect(box, 3, 3);
    owner->SetHighColor(DarkUI() ? rgb_color{240, 240, 240, 255} : rgb_color{30, 30, 30, 255});
    BFont bold(&BoldFont()); bold.SetSize(std::max(8.0f, BoldFont().Size() * 0.85f));
    owner->SetFont(&bold);
    std::string letter(1, code);
    owner->DrawString(letter.c_str(), BPoint(box.left + (box.Width() - bold.StringWidth(letter.c_str())) / 2, baseline - 1));
    owner->SetFont(&PlainFont()); owner->SetHighColor(text);
    std::string label = fFile.path;
    if (!fFile.originalPath.empty()) label = fFile.originalPath + " \xe2\x86\x92 " + fFile.path;
    BString truncated(label.c_str());
    PlainFont().TruncateString(&truncated, B_TRUNCATE_BEGINNING, std::max(40.0f, frame.Width() - 36));
    owner->DrawString(truncated.String(), BPoint(box.right + 8, baseline));
}
// ---------------------------------------------------------------- FileListView
FileListView::FileListView(const char* name, bool stagedSide, BMessenger target)
    : BListView(name, B_MULTIPLE_SELECTION_LIST), fStagedSide(stagedSide), fTarget(target) {
    SetSelectionMessage(new BMessage(kFileSelected));
    SetInvocationMessage(new BMessage(kFileInvoked));
}
std::vector<FileStatus> FileListView::SelectedFiles() const {
    std::vector<FileStatus> files;
    int32 index;
    for (int32 i = 0; (index = CurrentSelection(i)) >= 0; ++i) {
        auto item = dynamic_cast<FileItem*>(ItemAt(index));
        if (item) files.push_back(item->File());
    }
    return files;
}
bool FileListView::InitiateDrag(BPoint, int32 index, bool wasSelected) {
    if (!wasSelected) Select(index);
    auto files = SelectedFiles();
    if (files.empty()) return false;
    BMessage drag(kDragFiles);
    drag.AddBool("staged", fStagedSide);
    for (const auto& f : files) drag.AddString("path", f.path.c_str());
    BRect frame = ItemFrame(index);
    if (files.size() > 1) frame.bottom = frame.top + frame.Height() * std::min<size_t>(files.size(), 3);
    DragMessage(&drag, frame, this);
    return true;
}
void FileListView::MouseDown(BPoint where) {
    int32 buttons = 0;
    if (Window() && Window()->CurrentMessage()) Window()->CurrentMessage()->FindInt32("buttons", &buttons);
    if (buttons & B_SECONDARY_MOUSE_BUTTON) {
        int32 index = IndexOf(where);
        if (index >= 0 && !IsItemSelected(index)) Select(index);
        auto files = SelectedFiles();
        if (files.empty()) return;
        bool conflict = std::any_of(files.begin(), files.end(), [](const FileStatus& f) { return f.conflicted(); });
        bool untracked = std::all_of(files.begin(), files.end(), [](const FileStatus& f) { return f.untracked(); });
        std::vector<MenuChoice> choices;
        if (conflict) choices = {{"Resolve Using Ours (current branch)", kResolveOurs}, {"Resolve Using Theirs (incoming)", kResolveTheirs}, {"Mark as Resolved", kMarkResolved}, {"Open in Editor", kOpenInEditor, true, true}};
        else if (fStagedSide) choices = {{"Unstage", kUnstage}, {"Open in Editor", kOpenInEditor, true, true}, {"Show in Tracker", kOpenInTracker}};
        else choices = {{"Stage", kStage}, {untracked ? "Delete File" : "Discard Changes", kDiscard}, {"Open in Editor", kOpenInEditor, true, true}, {"Show in Tracker", kOpenInTracker}};
        if (untracked && !fStagedSide) choices.push_back({"Add to .gitignore", kIgnoreFile, true, true});
        uint32 command = PopupChoice(this, ConvertToScreen(where), choices);
        if (command) { BMessage message(command); message.AddBool("staged", fStagedSide); for (const auto& f : files) message.AddString("path", f.path.c_str()); fTarget.SendMessage(&message); }
        return;
    }
    BListView::MouseDown(where);
}
void FileListView::KeyDown(const char* bytes, int32 count) {
    if (count == 1 && (bytes[0] == B_SPACE || bytes[0] == B_ENTER || bytes[0] == B_RETURN)) { Invoke(); return; }
    BListView::KeyDown(bytes, count);
}
void FileListView::MessageReceived(BMessage* message) {
    if (message->WasDropped() && message->what == kDragFiles) {
        bool fromStaged = false; message->FindBool("staged", &fromStaged);
        if (fromStaged == fStagedSide) return;   // dropped back on its own list
        BMessage action(fStagedSide ? kStage : kUnstage);
        const char* path;
        for (int32 i = 0; message->FindString("path", i, &path) == B_OK; ++i) action.AddString("path", path);
        fTarget.SendMessage(&action);
        return;
    }
    BListView::MessageReceived(message);
}
// ---------------------------------------------------------------- WorkingCopyView
WorkingCopyView::WorkingCopyView(BMessenger window) : BView("workingCopy", B_WILL_DRAW), fWindow(window) {
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    fUnstaged = new FileListView("unstaged", false, BMessenger(this));
    fStaged = new FileListView("staged", true, BMessenger(this));
    fUnstagedTitle = new BStringView("unstagedTitle", "Unstaged changes");
    fUnstagedTitle->SetFont(&BoldFont());
    fUnstagedTitle->SetExplicitMinSize(BSize(130, B_SIZE_UNSET));
    fStagedTitle = new BStringView("stagedTitle", "Staged changes");
    fStagedTitle->SetFont(&BoldFont());
    fStagedTitle->SetExplicitMinSize(BSize(130, B_SIZE_UNSET));
    fStageAll = new BButton("stageAll", "Stage All", new BMessage(kStageAll));
    fUnstageAll = new BButton("unstageAll", "Unstage All", new BMessage(kUnstageAll));
    fDiscardAll = new BButton("discardAll", "Discard All" B_UTF8_ELLIPSIS, new BMessage(kDiscardAll));
    fMessage = new BTextView("message");
    fMessage->SetWordWrap(true);
    fMessage->SetExplicitMinSize(BSize(200, 70));
    fMessage->SetExplicitPreferredSize(BSize(300, 90));
    auto messageScroll = new BScrollView("messageScroll", fMessage, 0, false, true);
    fAmend = new BCheckBox("amend", "Amend last commit", new BMessage(kAmendToggled));
    fCommit = new BButton("commit", "Commit", new BMessage(kCommit));
    fStateLabel = new BStringView("stateLabel", "");
    fStateLabel->SetFont(&BoldFont());
    fStateLabel->SetTruncation(B_TRUNCATE_END); fStateLabel->SetExplicitMinSize(BSize(60, B_SIZE_UNSET));
    fStateBar = new BView("stateBar", B_WILL_DRAW);
    fStateBar->SetViewColor(Palette::Current().conflict);
    BLayoutBuilder::Group<>(fStateBar, B_HORIZONTAL, B_USE_SMALL_SPACING).SetInsets(B_USE_SMALL_INSETS)
        .Add(fStateLabel).AddGlue()
        .Add(new BButton("continue", "Continue", new BMessage(kContinueOperation)))
        .Add(new BButton("abort", "Abort", new BMessage(kAbortOperation)))
    .End();
    fStateBar->Hide();
    fConflictBar = new BView("conflictBar", B_WILL_DRAW);
    fConflictBar->SetViewColor(Palette::Current().conflict);
    fConflictLabel = new BStringView("conflictLabel", "");
    fConflictLabel->SetTruncation(B_TRUNCATE_END); fConflictLabel->SetExplicitMinSize(BSize(60, B_SIZE_UNSET));
    BLayoutBuilder::Group<>(fConflictBar, B_HORIZONTAL, B_USE_SMALL_SPACING).SetInsets(B_USE_SMALL_INSETS)
        .Add(fConflictLabel).AddGlue()
        .Add(new BButton("ours", "Use Ours", new BMessage(kResolveOurs)))
        .Add(new BButton("theirs", "Use Theirs", new BMessage(kResolveTheirs)))
        .Add(new BButton("editor", "Open in Editor", new BMessage(kOpenInEditor)))
        .Add(new BButton("resolved", "Mark Resolved", new BMessage(kMarkResolved)))
    .End();
    fConflictBar->Hide();
    fDiff = new DiffView();
    fDiff->SetTarget(BMessenger(this));
    auto diffScroll = new BScrollView("diffScroll", fDiff, 0, true, true, B_NO_BORDER);
    auto unstagedScroll = new BScrollView("unstagedScroll", fUnstaged, 0, false, true);
    auto stagedScroll = new BScrollView("stagedScroll", fStaged, 0, false, true);
    BView* lists = new BView("lists", 0);
    lists->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    BLayoutBuilder::Group<>(lists, B_VERTICAL, B_USE_SMALL_SPACING).SetInsets(B_USE_SMALL_INSETS)
        .AddGroup(B_HORIZONTAL).Add(fUnstagedTitle).AddGlue().Add(fDiscardAll).Add(fStageAll).End()
        .Add(unstagedScroll, 1.0f)
        .AddGroup(B_HORIZONTAL).Add(fStagedTitle).AddGlue().Add(fUnstageAll).End()
        .Add(stagedScroll, 1.0f)
        .Add(new BStringView("messageTitle", "Commit message"))
        .Add(messageScroll, 0.0f)
        .AddGroup(B_HORIZONTAL).Add(fAmend).AddGlue().Add(fCommit).End()
    .End();
    BView* right = new BView("right", 0);
    right->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    BLayoutBuilder::Group<>(right, B_VERTICAL, 0).Add(fStateBar).Add(fConflictBar).Add(diffScroll, 1.0f).End();
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .AddSplit(B_HORIZONTAL, 0.0f)
            .Add(lists, 0.34f)
            .Add(right, 0.66f)
        .End()
    .End();
    fCommit->MakeDefault(false);
}
void WorkingCopyView::AttachedToWindow() {
    BView::AttachedToWindow();
    for (BButton* b : {fStageAll, fUnstageAll, fDiscardAll, fCommit}) b->SetTarget(this);
    fAmend->SetTarget(this);
    fUnstaged->SetTarget(this); fStaged->SetTarget(this);
    // Messengers to this view are only valid once it belongs to a window.
    fUnstaged->SetActionTarget(BMessenger(this)); fStaged->SetActionTarget(BMessenger(this));
    fDiff->SetTarget(BMessenger(this));
    for (const char* name : {"continue", "abort", "ours", "theirs", "editor", "resolved"}) {
        if (auto button = dynamic_cast<BButton*>(FindView(name))) button->SetTarget(this);
    }
    UpdateButtons();
}
void WorkingCopyView::SetFiles(const std::vector<FileStatus>& files, const RepositoryState& state) {
    fFiles = files;
    // Preserve selections by path.
    std::vector<std::string> unstagedSelected, stagedSelected;
    for (const auto& f : fUnstaged->SelectedFiles()) unstagedSelected.push_back(f.path);
    for (const auto& f : fStaged->SelectedFiles()) stagedSelected.push_back(f.path);
    fSuppressSelection = true;
    fUnstaged->MakeEmpty(); fStaged->MakeEmpty();
    int unstaged = 0, staged = 0, conflicts = 0;
    for (const auto& f : files) {
        if (f.conflicted()) { fUnstaged->AddItem(new FileItem(f, false)); ++conflicts; ++unstaged; continue; }
        if (f.unstaged()) { fUnstaged->AddItem(new FileItem(f, false)); ++unstaged; }
        if (f.staged()) { fStaged->AddItem(new FileItem(f, true)); ++staged; }
    }
    for (int32 i = 0; i < fUnstaged->CountItems(); ++i) if (std::find(unstagedSelected.begin(), unstagedSelected.end(), static_cast<FileItem*>(fUnstaged->ItemAt(i))->File().path) != unstagedSelected.end()) fUnstaged->Select(i, true);
    for (int32 i = 0; i < fStaged->CountItems(); ++i) if (std::find(stagedSelected.begin(), stagedSelected.end(), static_cast<FileItem*>(fStaged->ItemAt(i))->File().path) != stagedSelected.end()) fStaged->Select(i, true);
    fSuppressSelection = false;
    fUnstagedTitle->SetText(("Unstaged changes (" + std::to_string(unstaged) + (conflicts ? ", " + std::to_string(conflicts) + " conflicted" : "") + ")").c_str());
    fStagedTitle->SetText(("Staged changes (" + std::to_string(staged) + ")").c_str());
    if (state.inProgress()) {
        fStateLabel->SetText((state.description() + (conflicts ? ": resolve the conflicted files, stage them, then continue." : ". Continue when ready, or abort to return to the previous state.")).c_str());
        if (fStateBar->IsHidden()) fStateBar->Show();
    } else if (!fStateBar->IsHidden()) fStateBar->Hide();
    // If the shown file vanished from its list, clear the diff.
    bool stillThere = false;
    for (const auto& f : files) if (f.path == fShownPath && ((fShownStaged && f.staged()) || (!fShownStaged && (f.unstaged() || f.conflicted())))) stillThere = true;
    if (!fShownPath.empty() && !stillThere) { fShownPath.clear(); fDiff->SetMessage(files.empty() ? "The working copy is clean." : "Select a change to see its diff."); if (!fConflictBar->IsHidden()) fConflictBar->Hide(); }
    if (fShownPath.empty() && files.empty()) fDiff->SetMessage("The working copy is clean.");
    UpdateButtons();
}
void WorkingCopyView::UpdateButtons() {
    fStageAll->SetEnabled(fUnstaged->CountItems() > 0);
    fDiscardAll->SetEnabled(fUnstaged->CountItems() > 0);
    fUnstageAll->SetEnabled(fStaged->CountItems() > 0);
    fCommit->SetEnabled(fStaged->CountItems() > 0 || fAmend->Value() == B_CONTROL_ON);
    fCommit->SetLabel(fAmend->Value() == B_CONTROL_ON ? "Amend Commit" : "Commit");
}
void WorkingCopyView::SetDiff(const std::string& unified, bool staged, bool untracked, const std::string& path) {
    fShownPath = path; fShownStaged = staged;
    fDiff->SetDiff(unified, untracked ? DiffView::kUntracked : staged ? DiffView::kStaged : DiffView::kUnstaged, path);
    if (!fConflictBar->IsHidden()) fConflictBar->Hide();
}
void WorkingCopyView::ShowConflict(const FileStatus& file) {
    fShownPath = file.path; fShownStaged = false;
    fConflictLabel->SetText((file.path + ": " + file.description(false)).c_str());
    if (fConflictBar->IsHidden()) fConflictBar->Show();
}
std::string WorkingCopyView::Message() const { return fMessage->Text(); }
bool WorkingCopyView::Amend() const { return fAmend->Value() == B_CONTROL_ON; }
void WorkingCopyView::SetAmend(bool amend, const std::string& previousMessage) {
    fAmend->SetValue(amend ? B_CONTROL_ON : B_CONTROL_OFF);
    if (amend && Trim(fMessage->Text()).empty()) fMessage->SetText(previousMessage.c_str());
    UpdateButtons();
}
void WorkingCopyView::ClearMessage() { fMessage->SetText(""); fAmend->SetValue(B_CONTROL_OFF); UpdateButtons(); }
void WorkingCopyView::FocusMessage() { fMessage->MakeFocus(true); }
bool WorkingCopyView::SelectedFile(FileStatus& file, bool& staged) const {
    for (const auto& f : fFiles) {
        if (f.path != fShownPath) continue;
        if (fShownStaged ? f.staged() : (f.unstaged() || f.conflicted())) { file = f; staged = fShownStaged; return true; }
    }
    return false;
}
std::vector<FileStatus> WorkingCopyView::Selection(bool staged) const { return staged ? fStaged->SelectedFiles() : fUnstaged->SelectedFiles(); }
void WorkingCopyView::Restyle() {
    Palette palette = Palette::Current();
    fStateBar->SetViewColor(palette.conflict); fConflictBar->SetViewColor(palette.conflict);
    for (FileListView* list : {fUnstaged, fStaged}) { list->SetFont(&PlainFont()); for (int32 i = 0; i < list->CountItems(); ++i) list->ItemAt(i)->Update(list, &PlainFont()); list->Invalidate(); }
    fDiff->Restyle();
}
void WorkingCopyView::SetBusy(bool busy) {
    fCommit->SetEnabled(!busy && (fStaged->CountItems() > 0 || fAmend->Value() == B_CONTROL_ON));
    fStageAll->SetEnabled(!busy && fUnstaged->CountItems() > 0);
    fUnstageAll->SetEnabled(!busy && fStaged->CountItems() > 0);
}
void WorkingCopyView::MessageReceived(BMessage* message) {
    switch (message->what) {
        case kFileSelected: {
            if (fSuppressSelection) break;
            // Only one list keeps a selection; the other clears so the diff is unambiguous.
            BListView* source = nullptr;
            if (message->FindPointer("source", reinterpret_cast<void**>(&source)) != B_OK) break;
            FileListView* list = source == fStaged ? fStaged : fUnstaged;
            FileListView* other = list == fStaged ? fUnstaged : fStaged;
            if (list->CurrentSelection() >= 0 && other->CurrentSelection() >= 0) { fSuppressSelection = true; other->DeselectAll(); fSuppressSelection = false; }
            auto files = list->SelectedFiles();
            if (files.empty()) break;
            if (files.back().path == fShownPath && list->StagedSide() == fShownStaged && !fShownPath.empty() && message->HasBool("refresh")) break;
            BMessage request(kDiffSelection);
            request.AddString("path", files.back().path.c_str());
            request.AddBool("staged", list->StagedSide());
            request.AddBool("untracked", files.back().untracked());
            request.AddBool("conflicted", files.back().conflicted());
            fWindow.SendMessage(&request);
            break;
        }
        case kFileInvoked: {
            BListView* source = nullptr;
            if (message->FindPointer("source", reinterpret_cast<void**>(&source)) != B_OK) break;
            FileListView* list = source == fStaged ? fStaged : fUnstaged;
            auto files = list->SelectedFiles();
            if (files.empty()) break;
            bool conflicted = std::any_of(files.begin(), files.end(), [](const FileStatus& f) { return f.conflicted(); });
            BMessage action(conflicted ? kOpenInEditor : list->StagedSide() ? kUnstage : kStage);
            for (const auto& f : files) action.AddString("path", f.path.c_str());
            fWindow.SendMessage(&action);
            break;
        }
        case kAmendToggled: {
            UpdateButtons();
            BMessage forward(kAmendToggled); forward.AddBool("amend", fAmend->Value() == B_CONTROL_ON);
            fWindow.SendMessage(&forward);
            break;
        }
        case kStage: case kUnstage: case kDiscard: case kStageAll: case kUnstageAll: case kDiscardAll: case kCommit:
        case kStageHunk: case kUnstageHunk: case kDiscardHunk: case kStageLines: case kUnstageLines: case kDiscardLines:
        case kResolveOurs: case kResolveTheirs: case kMarkResolved: case kOpenInEditor: case kOpenInTracker: case kIgnoreFile:
        case kContinueOperation: case kAbortOperation: {
            BMessage forward(*message);
            if (!forward.HasString("path") && !fShownPath.empty() && message->what != kStageAll && message->what != kUnstageAll
                && message->what != kDiscardAll && message->what != kCommit && message->what != kContinueOperation && message->what != kAbortOperation)
                forward.AddString("path", fShownPath.c_str());
            fWindow.SendMessage(&forward);
            break;
        }
        default: BView::MessageReceived(message);
    }
}
}
