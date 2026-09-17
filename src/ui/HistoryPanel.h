#pragma once
#include "core/Git.h"
#include "ui/Widgets.h"
#include <ListView.h>
#include <Messenger.h>
#include <View.h>
#include <vector>
class BTextView; class BTextControl; class BStringView;
namespace tc {
class HistoryView; class DiffView;
// History card: search field, the graph list, and the selected commit's
// metadata, file list and per-file diff.
class HistoryPanel : public BView {
public:
    explicit HistoryPanel(BMessenger window);
    void AttachedToWindow() override;
    void MessageReceived(BMessage* message) override;
    HistoryView* History() const { return fHistory; }
    DiffView* Diff() const { return fDiff; }
    void SetTitle(const std::string& title);
    void ShowCommit(const Commit& commit, const std::vector<CommitFile>& files);
    void ShowRange(const std::string& from, const std::string& to, const std::vector<CommitFile>& files);
    void ShowStash(const Stash& stash, const std::vector<CommitFile>& files);
    void ClearDetails(const std::string& message);
    const CommitFile* SelectedFile() const;
    std::string CurrentHash() const { return fHash; }
    std::string RangeFrom() const { return fFrom; }
    std::string RangeTo() const { return fTo; }
    std::string SearchText() const;
private:
    BMessenger fWindow;
    BStringView* fTitle;
    BTextControl* fSearch;
    HistoryView* fHistory;
    BTextView* fInfo;
    BListView* fFiles;
    DiffView* fDiff;
    std::vector<CommitFile> fCommitFiles;
    std::string fHash, fFrom, fTo;
};
}
