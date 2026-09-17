#include "ui/HistoryPanel.h"
#include "ui/DiffView.h"
#include "ui/HistoryView.h"
#include "ui/Messages.h"
#include "ui/Widgets.h"
#include <LayoutBuilder.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <cmath>

namespace tc {
namespace {
class CommitFileItem : public BStringItem {
public:
    explicit CommitFileItem(const CommitFile& file) : BStringItem(""), fFile(file) {
        std::string label = std::string(1, file.status) + "  " + (file.originalPath.empty() ? file.path : file.originalPath + " \xe2\x86\x92 " + file.path);
        if (file.binary) label += "  (binary)";
        else if (file.added || file.deleted) label += "  +" + std::to_string(file.added) + " -" + std::to_string(file.deleted);
        SetText(label.c_str());
    }
    const CommitFile& File() const { return fFile; }
private:
    CommitFile fFile;
};
}
HistoryPanel::HistoryPanel(BMessenger window) : BView("historyPanel", B_WILL_DRAW), fWindow(window) {
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    fTitle = new BStringView("historyTitle", "History");
    fTitle->SetFont(&BoldFont());
    fTitle->SetTruncation(B_TRUNCATE_END); fTitle->SetExplicitMinSize(BSize(60, B_SIZE_UNSET));
    fSearch = new BTextControl("search", "Search:", "", new BMessage(kSearchHistory));
    fSearch->SetModificationMessage(new BMessage(kSearchHistory));
    fHistory = new HistoryView();
    fHistory->SetTarget(window);
    fInfo = new BTextView("commitInfo");
    fInfo->MakeEditable(false);
    fInfo->SetWordWrap(true);
    fInfo->SetInsets(6, 6, 6, 6);
    fInfo->SetExplicitMinSize(BSize(200, 60));
    fFiles = new BListView("commitFiles", B_SINGLE_SELECTION_LIST);
    fFiles->SetSelectionMessage(new BMessage(kCommitFileSelected));
    fDiff = new DiffView();
    fDiff->SetTarget(window);
    auto historyScroll = new BScrollView("historyScroll", fHistory, 0, false, true, B_NO_BORDER);
    auto infoScroll = new BScrollView("infoScroll", fInfo, 0, false, true);
    auto filesScroll = new BScrollView("filesScroll", fFiles, 0, false, true);
    auto diffScroll = new BScrollView("commitDiffScroll", fDiff, 0, true, true, B_NO_BORDER);
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING).SetInsets(B_USE_SMALL_INSETS).Add(fTitle).AddGlue().Add(fSearch).End()
        .AddSplit(B_VERTICAL, 0.0f)
            .Add(historyScroll, 0.55f)
            .AddSplit(B_HORIZONTAL, 0.0f, 0.45f)
                .AddGroup(B_VERTICAL, B_USE_SMALL_SPACING, 0.35f).SetInsets(B_USE_SMALL_INSETS).Add(infoScroll, 0.45f).Add(filesScroll, 0.55f).End()
                .Add(diffScroll, 0.65f)
            .End()
        .End()
    .End();
}
void HistoryPanel::AttachedToWindow() {
    BView::AttachedToWindow();
    fSearch->SetTarget(this);
    fFiles->SetTarget(this);
}
void HistoryPanel::Restyle() {
    fHistory->Restyle(); fDiff->Restyle();
    fFiles->SetFont(&PlainFont());
    Palette palette = Palette::Current();
    fInfo->SetFontAndColor(&PlainFont(), B_FONT_ALL, &palette.text);
    fInfo->SetViewColor(palette.background); fInfo->SetLowColor(palette.background);
}
std::string HistoryPanel::SearchText() const { return fSearch->Text(); }
void HistoryPanel::SetTitle(const std::string& title) { fTitle->SetText(title.c_str()); }
void HistoryPanel::ShowCommit(const Commit& commit, const std::vector<CommitFile>& files) {
    fHash = commit.hash; fFrom.clear(); fTo.clear();
    std::string text = commit.subject + "\n\n";
    if (!commit.body.empty()) text += commit.body + "\n\n";
    text += "Commit: " + commit.hash + "\n";
    for (const auto& p : commit.parents) text += "Parent: " + p + "\n";
    text += "Author: " + commit.authorName + " <" + commit.authorEmail + ">\n";
    text += "Date:   " + FormatDate(commit.authorDate) + " (" + FormatDate(commit.authorDate, true) + ")\n";
    if (!commit.refs.empty()) { text += "Refs:   "; for (size_t i = 0; i < commit.refs.size(); ++i) text += (i ? ", " : "") + commit.refs[i]; text += "\n"; }
    fInfo->SetText(text.c_str());
    BFont bold(&BoldFont());
    fInfo->SetFontAndColor(0, commit.subject.size(), &bold);
    fCommitFiles = files;
    fFiles->MakeEmpty();
    for (const auto& f : files) fFiles->AddItem(new CommitFileItem(f));
    fDiff->SetMessage(files.empty() ? "This commit changes no files." : "Select a file to see its changes in this commit.");
    if (!files.empty()) fFiles->Select(0);
}
void HistoryPanel::ShowRange(const std::string& from, const std::string& to, const std::vector<CommitFile>& files) {
    fHash.clear(); fFrom = from; fTo = to;
    fInfo->SetText(("Comparing " + from.substr(0, 8) + " with " + to.substr(0, 8) + "\n\n" + std::to_string(files.size()) + " files differ.").c_str());
    fCommitFiles = files;
    fFiles->MakeEmpty();
    for (const auto& f : files) fFiles->AddItem(new CommitFileItem(f));
    fDiff->SetMessage(files.empty() ? "No differences." : "Select a file to see its differences.");
    if (!files.empty()) fFiles->Select(0);
}
void HistoryPanel::ShowStash(const Stash& stash, const std::vector<CommitFile>& files) {
    ShowRange(stash.hash + "^", stash.hash, files);
    fInfo->SetText((stash.message + "\n\nStash " + stash.ref + " saved " + FormatDate(stash.date) + "\n" + std::to_string(files.size()) + " files changed.").c_str());
}
void HistoryPanel::ClearDetails(const std::string& message) {
    fHash.clear(); fFrom.clear(); fTo.clear();
    fInfo->SetText(message.c_str());
    fCommitFiles.clear(); fFiles->MakeEmpty();
    fDiff->SetMessage("");
}
const CommitFile* HistoryPanel::SelectedFile() const {
    auto item = dynamic_cast<CommitFileItem*>(fFiles->ItemAt(fFiles->CurrentSelection()));
    return item ? &item->File() : nullptr;
}
void HistoryPanel::MessageReceived(BMessage* message) {
    switch (message->what) {
        case kSearchHistory: fHistory->SetFilter(fSearch->Text()); break;
        case kCommitFileSelected: {
            const CommitFile* file = SelectedFile();
            if (!file) break;
            BMessage forward(kCommitFileSelected);
            forward.AddString("path", file->path.c_str());
            forward.AddString("hash", fHash.c_str());
            forward.AddString("from", fFrom.c_str());
            forward.AddString("to", fTo.c_str());
            fWindow.SendMessage(&forward);
            break;
        }
        default: BView::MessageReceived(message);
    }
}
}
