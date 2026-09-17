#pragma once
#include "core/Git.h"
#include "core/GitHub.h"
#include "core/Undo.h"
#include "ui/Async.h"
#include "ui/Sidebar.h"
#include <MessageRunner.h>
#include <Window.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>
class BCardLayout; class BMenuBar; class BMenuItem; class BStringView; class BButton; class BMenu;
namespace tc {
class WorkingCopyView; class HistoryPanel; class PullRequestsView;
// One repository. Owns the Git client, the background queues, the undo
// stack, and routes every command from the sidebar, cards, menus and drops.
class RepositoryWindow : public BWindow {
public:
    explicit RepositoryWindow(std::string root);
    ~RepositoryWindow() override;
    const std::string& Root() const { return fRoot; }
    void MessageReceived(BMessage* message) override;
    bool QuitRequested() override;
    void WindowActivated(bool active) override;
    void MenusBeginning() override;
private:
    enum Card { kCardWorkingCopy = 0, kCardHistory = 1, kCardPullRequests = 2 };
    // Setup
    void BuildMenu();
    BView* BuildToolbar();
    void ShowCard(Card card);
    // Data
    void Refresh();
    void LoadHistory(bool reset, bool keepPosition = false);
    void LoadWorkingDiff(const std::string& path, bool staged, bool untracked, bool conflicted);
    void LoadCommit(const std::string& hash);
    void LoadCommitDiff(const std::string& hash, const std::string& from, const std::string& to, const std::string& path);
    void LoadPullRequests(bool force = false);
    void LoadPullRequest(int number);
    std::vector<std::string> HistoryRevisions() const;
    // Operations
    using Operation = std::function<ProcessResult(const std::atomic<bool>&)>;
    void RunOperation(const std::string& title, Operation operation, std::function<void(const ProcessResult&)> done = {}, bool network = false);
    void RecordUndo(std::string description, std::function<ProcessResult()> undo);
    void SetStatus(const std::string& text);
    GitOptions Options() const;
    std::shared_ptr<GitRepository> Repo() const { return fRepo; }
    std::unique_ptr<GitHubClient> GitHub() const;
    // Command handlers (RepositoryWindowActions.cpp)
    void HandleWorkingCopy(BMessage* message);
    void HandleBranches(BMessage* message);
    void HandleHistory(BMessage* message);
    void HandleRemote(BMessage* message);
    void HandlePullRequests(BMessage* message);
    void HandleSidebarInvoke(BMessage* message);
    void HandleDrop(BMessage* message);
    void Undo();
    std::vector<std::string> Paths(BMessage* message) const;
    void Prompt(const std::string& title, const char* label, const std::string& initial, BMessage reply, const char* secondLabel = nullptr,
        const std::string& secondInitial = {}, const char* action = "OK", bool multiline = false, const std::vector<std::string>& choices = {}, const char* choiceLabel = nullptr);
    std::string BranchAt(const std::string& hash) const;
    void MergeInto(const std::string& source, const std::string& target);
    void RebaseOnto(const std::string& branch, const std::string& onto);
    void CheckoutBranch(const std::string& name);
    void DeleteBranch(const std::string& name, bool remote);
    void CherryPickOnto(const std::vector<std::string>& hashes, const std::string& branch);
    void PushBranch(const std::string& branch, const std::string& remote, bool force);
    void ApplyStash(const std::string& ref, bool pop);
    std::string fRoot, fTitleName;
    std::shared_ptr<GitRepository> fRepo;
    std::unique_ptr<AsyncQueue> fJobs, fOps, fNetwork;
    UndoStack fUndo;
    // Views
    Sidebar* fSidebar;
    BCardLayout* fCards;
    WorkingCopyView* fWorkingCopy;
    HistoryPanel* fHistory;
    PullRequestsView* fPullRequests;
    BStringView* fStatus;
    BStringView* fBranchLabel;
    BMenuBar* fMenuBar;
    BMenuItem* fUndoItem;
    BButton* fUndoButton;
    BButton* fPushButton;
    BButton* fPullButton;
    std::unique_ptr<BMessageRunner> fRunner;
    // State
    SidebarModel fModel;
    std::vector<FileStatus> fFiles;
    std::string fGitHubRepository;
    std::vector<std::string> fHistoryRevisions;
    std::string fHistoryTitle, fRefsSignature;
    size_t fHistoryLoaded = 0;
    bool fHistoryComplete = false, fHistoryLoading = false, fBusy = false, fRefreshing = false, fRefreshQueued = false;
    int64 fGeneration = 0, fDiffRequest = 0, fCommitRequest = 0, fPullRequestRequest = 0;
    std::string fPreviousCommitMessage;
    Card fCard = kCardWorkingCopy;
    SidebarEntry fSelectedEntry;
    std::vector<PullRequest> fPullRequestList;
    bigtime_t fPullRequestsLoadedAt = 0;
    std::string fPullRequestsLoadedState;
};
}
