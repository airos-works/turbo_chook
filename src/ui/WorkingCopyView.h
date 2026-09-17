#pragma once
#include "core/Git.h"
#include "ui/Widgets.h"
#include <ListView.h>
#include <Messenger.h>
#include <View.h>
#include <string>
#include <vector>
class BTextView; class BButton; class BCheckBox; class BStringView; class BGroupView; class BSplitView;
namespace tc {
class DiffView;
// A file entry in the unstaged or staged list; drag between the lists to stage.
class FileItem : public BListItem {
public:
    FileItem(FileStatus file, bool stagedSide) : fFile(std::move(file)), fStagedSide(stagedSide) {}
    void DrawItem(BView* owner, BRect frame, bool complete) override;
    void Update(BView* owner, const BFont* font) override;
    const FileStatus& File() const { return fFile; }
private:
    FileStatus fFile; bool fStagedSide;
};
// List that starts drags of its selected files and accepts drops from its sibling.
class FileListView : public BListView {
public:
    FileListView(const char* name, bool stagedSide, BMessenger target);
    bool InitiateDrag(BPoint point, int32 index, bool wasSelected) override;
    void MessageReceived(BMessage* message) override;
    void MouseDown(BPoint where) override;
    void KeyDown(const char* bytes, int32 count) override;
    std::vector<FileStatus> SelectedFiles() const;
    bool StagedSide() const { return fStagedSide; }
    void SetActionTarget(BMessenger target) { fTarget = target; }
private:
    bool fStagedSide; BMessenger fTarget;
};
// Tower's working copy: unstaged and staged change lists, the commit message
// composer, and the diff of the selected file with hunk/line staging.
class WorkingCopyView : public BView {
public:
    explicit WorkingCopyView(BMessenger window);
    void AttachedToWindow() override;
    void MessageReceived(BMessage* message) override;
    void SetFiles(const std::vector<FileStatus>& files, const RepositoryState& state);
    void SetDiff(const std::string& unified, bool staged, bool untracked, const std::string& path);
    void ShowConflict(const FileStatus& file);
    std::string Message() const;
    bool Amend() const;
    void SetAmend(bool amend, const std::string& previousMessage);
    void ClearMessage();
    // The file whose diff is shown, and whether it came from the staged list.
    bool SelectedFile(FileStatus& file, bool& staged) const;
    std::vector<FileStatus> Selection(bool staged) const;
    DiffView* Diff() const { return fDiff; }
    void SetBusy(bool busy);
    void FocusMessage();
private:
    void UpdateButtons();
    BMessenger fWindow;
    FileListView* fUnstaged;
    FileListView* fStaged;
    BStringView* fUnstagedTitle;
    BStringView* fStagedTitle;
    BButton* fStageAll;
    BButton* fUnstageAll;
    BButton* fDiscardAll;
    BTextView* fMessage;
    BCheckBox* fAmend;
    BButton* fCommit;
    BStringView* fStateLabel;
    BView* fConflictBar;
    BStringView* fConflictLabel;
    BView* fStateBar;
    DiffView* fDiff;
    std::vector<FileStatus> fFiles;
    std::string fShownPath; bool fShownStaged = false;
    bool fSuppressSelection = false;
};
}
