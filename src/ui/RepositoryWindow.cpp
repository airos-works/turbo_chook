#include "ui/RepositoryWindow.h"
#include "ui/Application.h"
#include "ui/DiffView.h"
#include "ui/HistoryPanel.h"
#include "ui/HistoryView.h"
#include "ui/Messages.h"
#include "ui/PullRequestsView.h"
#include "ui/Widgets.h"
#include "ui/WorkingCopyView.h"
#include <Autolock.h>
#include <Button.h>
#include <CardLayout.h>
#include <LayoutBuilder.h>
#include <Menu.h>
#include <MenuBar.h>
#include <MenuItem.h>
#include <ScrollView.h>
#include <SeparatorView.h>
#include <StringView.h>
#include <algorithm>

namespace tc {
RepositoryWindow::RepositoryWindow(std::string root)
    : BWindow(BRect(80, 80, 1240, 800), "Turbo Chook", B_DOCUMENT_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS), fRoot(std::move(root)) {
    auto slash = fRoot.rfind('/');
    fTitleName = slash == std::string::npos ? fRoot : fRoot.substr(slash + 1);
    SetTitle((fTitleName + " - Turbo Chook").c_str());
    fRepo = std::make_shared<GitRepository>(fRoot, Options());
    fJobs.reset(new AsyncQueue(BMessenger(this), 2));
    fOps.reset(new AsyncQueue(BMessenger(this), 1));
    fNetwork.reset(new AsyncQueue(BMessenger(this), 1));
    fSidebar = new Sidebar();
    fSidebar->SetDropTarget(BMessenger(this));
    fWorkingCopy = new WorkingCopyView(BMessenger(this));
    fHistory = new HistoryPanel(BMessenger(this));
    fPullRequests = new PullRequestsView(BMessenger(this));
    fStatus = new BStringView("status", "Loading" B_UTF8_ELLIPSIS);
    fBranchLabel = new BStringView("branch", "");
    fBranchLabel->SetFont(be_bold_font);
    BuildMenu();
    BView* toolbar = BuildToolbar();
    auto sidebarScroll = new BScrollView("sidebarScroll", fSidebar, 0, false, true, B_NO_BORDER);
    fCards = new BCardLayout();
    BView* cardHost = new BView("cards", 0);
    cardHost->SetLayout(fCards);
    fCards->AddView(fWorkingCopy);
    fCards->AddView(fHistory);
    fCards->AddView(fPullRequests);
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
        .Add(fMenuBar)
        .Add(toolbar)
        .Add(new BSeparatorView(B_HORIZONTAL))
        .AddSplit(B_HORIZONTAL, 0.0f)
            .Add(sidebarScroll, 0.2f)
            .Add(cardHost, 0.8f)
        .End()
        .Add(new BSeparatorView(B_HORIZONTAL))
        .AddGroup(B_HORIZONTAL, B_USE_SMALL_SPACING).SetInsets(B_USE_SMALL_INSETS, 2, B_USE_SMALL_INSETS, 2).Add(fBranchLabel).Add(fStatus).AddGlue().End()
    .End();
    fSidebar->SetExplicitMinSize(BSize(180, 100));
    fCards->SetVisibleItem(static_cast<int32>(kCardWorkingCopy));
    {
        BAutolock lock(App()->SettingsLock());
        BRect frame;
        Settings& s = App()->Preferences();
        if (s.GetInt("windowWidth") > 400 && s.GetInt("windowHeight") > 300) {
            frame.Set(s.GetInt("windowLeft"), s.GetInt("windowTop"), s.GetInt("windowLeft") + s.GetInt("windowWidth"), s.GetInt("windowTop") + s.GetInt("windowHeight"));
            MoveTo(frame.LeftTop()); ResizeTo(frame.Width(), frame.Height());
        }
    }
    fRunner.reset(new BMessageRunner(BMessenger(this), new BMessage(kRefreshTick), 4000000));
    Refresh();
}
RepositoryWindow::~RepositoryWindow() {}
GitOptions RepositoryWindow::Options() const {
    GitOptions options;
    options.executable = App()->GitExecutable();
    options.gitHubToken = App()->GitHubToken();
    return options;
}
std::unique_ptr<GitHubClient> RepositoryWindow::GitHub() const {
    GitHubOptions options;
    options.token = App()->GitHubToken();
    { BAutolock lock(App()->SettingsLock()); options.apiBase = App()->Preferences().Get("gitHubApi", "https://api.github.com"); }
    return std::unique_ptr<GitHubClient>(new GitHubClient(options, fGitHubRepository));
}
void RepositoryWindow::BuildMenu() {
    fMenuBar = new BMenuBar("menu");
    BMenu* file = new BMenu("File");
    file->AddItem(new BMenuItem("Open Repository" B_UTF8_ELLIPSIS, new BMessage(kOpenRepository), 'O'));
    file->AddItem(new BMenuItem("Clone Repository" B_UTF8_ELLIPSIS, new BMessage(kCloneRepository), 'O', B_SHIFT_KEY));
    file->AddItem(new BMenuItem("New Repository" B_UTF8_ELLIPSIS, new BMessage(kNewRepository)));
    file->AddSeparatorItem();
    file->AddItem(new BMenuItem("Show in Tracker", new BMessage(kOpenInTracker)));
    file->AddItem(new BMenuItem("Open Terminal Here", new BMessage(kOpenTerminal), 'T', B_SHIFT_KEY));
    file->AddSeparatorItem();
    file->AddItem(new BMenuItem("Preferences" B_UTF8_ELLIPSIS, new BMessage(kShowPreferences), ','));
    file->AddSeparatorItem();
    file->AddItem(new BMenuItem("Close", new BMessage(B_QUIT_REQUESTED), 'W'));
    file->AddItem(new BMenuItem("Quit", new BMessage(kQuitApp), 'Q'));
    fMenuBar->AddItem(file);
    BMenu* edit = new BMenu("Edit");
    fUndoItem = new BMenuItem("Undo", new BMessage(kUndo), 'Z');
    fUndoItem->SetEnabled(false);
    edit->AddItem(fUndoItem);
    edit->AddSeparatorItem();
    edit->AddItem(new BMenuItem("Refresh", new BMessage(kRefresh), 'R'));
    fMenuBar->AddItem(edit);
    BMenu* view = new BMenu("View");
    view->AddItem(new BMenuItem("Working Copy", new BMessage(kViewWorkingCopy), '1'));
    view->AddItem(new BMenuItem("History", new BMessage(kViewHistory), '2'));
    view->AddItem(new BMenuItem("Pull Requests", new BMessage(kViewPullRequests), '3'));
    fMenuBar->AddItem(view);
    BMenu* repository = new BMenu("Repository");
    repository->AddItem(new BMenuItem("Fetch All Remotes", new BMessage(kFetchAll), 'F', B_SHIFT_KEY));
    repository->AddItem(new BMenuItem("Pull", new BMessage(kPull), 'L', B_SHIFT_KEY));
    repository->AddItem(new BMenuItem("Push", new BMessage(kPush), 'P', B_SHIFT_KEY));
    repository->AddSeparatorItem();
    repository->AddItem(new BMenuItem("Stash Changes" B_UTF8_ELLIPSIS, new BMessage(kSaveStash), 'S', B_SHIFT_KEY));
    repository->AddItem(new BMenuItem("Apply Latest Stash", new BMessage(kPopStash)));
    repository->AddSeparatorItem();
    repository->AddItem(new BMenuItem("Add Remote" B_UTF8_ELLIPSIS, new BMessage(kAddRemote)));
    repository->AddItem(new BMenuItem("Update Submodules", new BMessage(kUpdateSubmodules)));
    repository->AddSeparatorItem();
    repository->AddItem(new BMenuItem("Continue Operation", new BMessage(kContinueOperation)));
    repository->AddItem(new BMenuItem("Abort Operation", new BMessage(kAbortOperation)));
    fMenuBar->AddItem(repository);
    BMenu* branch = new BMenu("Branch");
    branch->AddItem(new BMenuItem("New Branch" B_UTF8_ELLIPSIS, new BMessage(kNewBranch), 'B'));
    branch->AddItem(new BMenuItem("Checkout" B_UTF8_ELLIPSIS, new BMessage(kCheckoutBranch)));
    branch->AddItem(new BMenuItem("Merge Into Current" B_UTF8_ELLIPSIS, new BMessage(kMergeBranch), 'M'));
    branch->AddItem(new BMenuItem("Rebase Current Onto" B_UTF8_ELLIPSIS, new BMessage(kRebaseBranch)));
    branch->AddItem(new BMenuItem("Rename Current" B_UTF8_ELLIPSIS, new BMessage(kRenameBranch)));
    branch->AddItem(new BMenuItem("Delete" B_UTF8_ELLIPSIS, new BMessage(kDeleteBranch)));
    branch->AddSeparatorItem();
    branch->AddItem(new BMenuItem("New Tag" B_UTF8_ELLIPSIS, new BMessage(kNewTag)));
    branch->AddSeparatorItem();
    branch->AddItem(new BMenuItem("Compare With Current" B_UTF8_ELLIPSIS, new BMessage(kCompareBranch)));
    fMenuBar->AddItem(branch);
    BMenu* commit = new BMenu("Commit");
    commit->AddItem(new BMenuItem("Stage All", new BMessage(kStageAll), 'A', B_SHIFT_KEY));
    commit->AddItem(new BMenuItem("Unstage All", new BMessage(kUnstageAll), 'U', B_SHIFT_KEY));
    commit->AddItem(new BMenuItem("Discard All Changes" B_UTF8_ELLIPSIS, new BMessage(kDiscardAll)));
    commit->AddSeparatorItem();
    commit->AddItem(new BMenuItem("Commit", new BMessage(kCommit), B_RETURN, B_SHIFT_KEY));
    commit->AddItem(new BMenuItem("Toggle Amend", new BMessage(kAmendToggled)));
    fMenuBar->AddItem(commit);
    BMenu* help = new BMenu("Help");
    help->AddItem(new BMenuItem("About Turbo Chook", new BMessage(B_ABOUT_REQUESTED)));
    fMenuBar->AddItem(help);
}
BView* RepositoryWindow::BuildToolbar() {
    BView* bar = new BView("toolbar", B_WILL_DRAW);
    bar->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    fPullButton = new BButton("pull", "Pull", new BMessage(kPull));
    fPushButton = new BButton("push", "Push", new BMessage(kPush));
    fUndoButton = new BButton("undo", "Undo", new BMessage(kUndo));
    fUndoButton->SetEnabled(false);
    BLayoutBuilder::Group<>(bar, B_HORIZONTAL, B_USE_SMALL_SPACING).SetInsets(B_USE_SMALL_INSETS, 4, B_USE_SMALL_INSETS, 4)
        .Add(new BButton("fetch", "Fetch", new BMessage(kFetchAll)))
        .Add(fPullButton).Add(fPushButton)
        .Add(new BSeparatorView(B_VERTICAL))
        .Add(new BButton("branch", "New Branch" B_UTF8_ELLIPSIS, new BMessage(kNewBranch)))
        .Add(new BButton("merge", "Merge" B_UTF8_ELLIPSIS, new BMessage(kMergeBranch)))
        .Add(new BButton("stash", "Stash" B_UTF8_ELLIPSIS, new BMessage(kSaveStash)))
        .Add(new BSeparatorView(B_VERTICAL))
        .Add(fUndoButton)
        .AddGlue()
        .Add(new BButton("refresh", "Refresh", new BMessage(kRefresh)))
    .End();
    return bar;
}
void RepositoryWindow::ShowCard(Card card) {
    fCard = card;
    fCards->SetVisibleItem(static_cast<int32>(card));
}
void RepositoryWindow::SetStatus(const std::string& text) { fStatus->SetText(text.c_str()); }
void RepositoryWindow::MenusBeginning() {
    fUndoItem->SetLabel(fUndo.Empty() ? "Undo" : fUndo.Description().c_str());
    fUndoItem->SetEnabled(!fUndo.Empty() && !fBusy);
}
void RepositoryWindow::WindowActivated(bool active) {
    BWindow::WindowActivated(active);
    if (active) Refresh();
}
bool RepositoryWindow::QuitRequested() {
    if (fBusy && !Confirm("Close Repository", "An operation is still running. Close anyway?", "Close")) return false;
    {
        BAutolock lock(App()->SettingsLock());
        Settings& s = App()->Preferences();
        s.SetInt("windowLeft", static_cast<int>(Frame().left)); s.SetInt("windowTop", static_cast<int>(Frame().top));
        s.SetInt("windowWidth", static_cast<int>(Frame().Width())); s.SetInt("windowHeight", static_cast<int>(Frame().Height()));
        s.Save();
    }
    fRunner.reset();
    fJobs->StopAndWait(); fOps->StopAndWait(); fNetwork->StopAndWait();
    be_app->PostMessage(kRepositoryClosed);
    return true;
}
// ---------------------------------------------------------------- data loading
void RepositoryWindow::Refresh() {
    if (fRefreshing) { fRefreshQueued = true; return; }
    fRefreshing = true;
    auto repo = fRepo;
    int64 generation = ++fGeneration;
    fJobs->Submit([repo, generation, this](const std::atomic<bool>& cancel) -> AsyncQueue::Callback {
        SidebarModel model;
        auto state = repo->State();
        auto status = repo->Status(&cancel);
        auto branches = repo->Branches(&cancel);
        auto tags = repo->Tags(&cancel);
        auto stashes = repo->Stashes(&cancel);
        auto remotes = repo->Remotes();
        auto submodules = repo->Submodules();
        std::string error;
        if (!state.ok()) error = state.error; else if (!status.ok()) error = status.error; else if (!branches.ok()) error = branches.error;
        model.state = state.value; model.branches = branches.value; model.tags = tags.value; model.stashes = stashes.value;
        model.remotes = remotes.value; model.submodules = submodules.value;
        model.changes = static_cast<int>(status.value.size());
        std::string github;
        for (const auto& r : model.remotes) { auto gh = GitHubRepository(r.fetchURL); if (gh) { github = *gh; if (r.name == "origin") break; } }
        model.hasGitHub = !github.empty();
        std::string lastMessage;
        auto head = repo->Show("HEAD");
        if (head.ok()) lastMessage = head.value.subject + (head.value.body.empty() ? "" : "\n\n" + head.value.body);
        auto files = status.value;
        return [=] {
            fRefreshing = false;
            if (generation != fGeneration) { if (fRefreshQueued) { fRefreshQueued = false; Refresh(); } return; }
            if (!error.empty()) { SetStatus(error); return; }
            fModel = model; fFiles = files; fGitHubRepository = github; fPreviousCommitMessage = lastMessage;
            fSidebar->SetModel(model);
            fWorkingCopy->SetFiles(files, model.state);
            fHistory->History()->SetCurrentHead(model.state.headHash);
            std::string branch = model.state.detached ? "Detached at " + model.state.head : model.state.head;
            const Branch* current = nullptr;
            for (const auto& b : model.branches) if (b.current) current = &b;
            std::string tracking;
            if (current && !current->upstream.empty()) {
                tracking = "  \xe2\x86\x92 " + current->upstream;
                if (current->ahead) tracking += "  \xe2\x86\x91" + std::to_string(current->ahead);
                if (current->behind) tracking += "  \xe2\x86\x93" + std::to_string(current->behind);
                if (current->gone) tracking += "  (gone)";
            }
            fBranchLabel->SetText(branch.c_str());
            std::string status = tracking;
            if (model.state.inProgress()) status += "   " + model.state.description();
            if (!fBusy) SetStatus(status.empty() ? std::to_string(files.size()) + (files.size() == 1 ? " change" : " changes") : status);
            fPushButton->SetLabel(current && current->upstream.empty() && !model.state.detached ? "Publish" : "Push");
            fPullButton->SetEnabled(current && !current->upstream.empty());
            fPullRequests->SetRepository(github, !App()->GitHubToken().empty());
            SetTitle((fTitleName + " (" + branch + ") - Turbo Chook").c_str());
            // Reload the history only when a ref actually moved, keeping pages, selection and scroll position.
            std::string refs = model.state.headHash + '\n';
            for (const auto& b : model.branches) refs += b.fullRef + ' ' + b.hash + '\n';
            for (const auto& t : model.tags) refs += t.name + ' ' + t.hash + '\n';
            for (const auto& s : model.stashes) refs += s.ref + ' ' + s.hash + '\n';
            bool refsChanged = refs != fRefsSignature;
            fRefsSignature = refs;
            if (fHistoryLoaded == 0 && !fHistoryLoading) LoadHistory(true);
            else if (refsChanged) LoadHistory(true, true);
            if (fRefreshQueued) { fRefreshQueued = false; Refresh(); }
        };
    }, "refresh");
}
std::vector<std::string> RepositoryWindow::HistoryRevisions() const { return fHistoryRevisions; }
void RepositoryWindow::LoadHistory(bool reset, bool keepPosition) {
    if (fHistoryLoading && !reset) return;
    const size_t page = 300;
    // A refresh that keeps the position re-reads everything loaded so far in one request.
    size_t count = reset && keepPosition ? std::max(page, fHistoryLoaded) : page;
    if (reset) { fHistoryLoaded = 0; fHistoryComplete = false; }
    fHistoryLoading = true;
    fHistory->History()->SetLoading(true, !fHistoryComplete);
    auto repo = fRepo;
    auto revisions = fHistoryRevisions;
    size_t skip = fHistoryLoaded;
    int64 generation = ++fGeneration;
    fJobs->Submit([repo, revisions, skip, count, generation, keepPosition, this](const std::atomic<bool>& cancel) -> AsyncQueue::Callback {
        auto log = repo->Log(revisions, skip, count, {}, &cancel);
        return [=] {
            fHistoryLoading = false;
            if (generation != fGeneration && skip == 0) return;
            if (!log.ok()) { fHistory->History()->SetLoading(false, false); SetStatus(log.error); return; }
            fHistoryComplete = log.value.size() < count;
            fHistoryLoaded += log.value.size();
            fHistory->History()->SetCommits(log.value, skip > 0, keepPosition);
            fHistory->History()->SetLoading(false, !fHistoryComplete);
        };
    }, "history");
}
void RepositoryWindow::LoadWorkingDiff(const std::string& path, bool staged, bool untracked, bool conflicted) {
    auto repo = fRepo;
    int64 request = ++fDiffRequest;
    int context = 3;
    { BAutolock lock(App()->SettingsLock()); context = App()->Preferences().GetInt("diffContext", 3); }
    FileStatus conflictFile;
    for (const auto& f : fFiles) if (f.path == path) conflictFile = f;
    fJobs->Submit([repo, path, staged, untracked, conflicted, conflictFile, context, request, this](const std::atomic<bool>& cancel) -> AsyncQueue::Callback {
        ProcessResult result = untracked ? repo->UntrackedDiff(path, &cancel) : repo->WorkingDiff(path, staged, &cancel, context);
        std::string text = result.output;
        if (!result.ok() && !untracked) text.clear();
        return [=] {
            if (request != fDiffRequest) return;
            if (!result.ok() && text.empty() && !untracked) { fWorkingCopy->Diff()->SetMessage(result.diagnostic()); return; }
            fWorkingCopy->SetDiff(text, staged, untracked, path);
            if (conflicted) fWorkingCopy->ShowConflict(conflictFile);
        };
    }, "diff");
}
void RepositoryWindow::LoadCommit(const std::string& hash) {
    auto repo = fRepo;
    int64 request = ++fCommitRequest;
    fJobs->Submit([repo, hash, request, this](const std::atomic<bool>& cancel) -> AsyncQueue::Callback {
        auto commit = repo->Show(hash);
        auto files = repo->CommitFiles(hash, &cancel);
        return [=] {
            if (request != fCommitRequest) return;
            if (!commit.ok()) { fHistory->ClearDetails(commit.error); return; }
            fHistory->ShowCommit(commit.value, files.value);
        };
    }, "commit");
}
void RepositoryWindow::LoadCommitDiff(const std::string& hash, const std::string& from, const std::string& to, const std::string& path) {
    auto repo = fRepo;
    int64 request = ++fDiffRequest;
    fJobs->Submit([repo, hash, from, to, path, request, this](const std::atomic<bool>& cancel) -> AsyncQueue::Callback {
        ProcessResult result = hash.empty() ? repo->RangeDiff(from, to, path, &cancel) : repo->CommitDiff(hash, path, &cancel);
        return [=] {
            if (request != fDiffRequest) return;
            if (!result.ok()) { fHistory->Diff()->SetMessage(result.diagnostic()); return; }
            fHistory->Diff()->SetDiff(result.output, DiffView::kReadOnly, path);
        };
    }, "diff");
}
void RepositoryWindow::LoadPullRequests(bool force) {
    if (fGitHubRepository.empty()) { fPullRequests->SetPullRequests({}, "This repository has no GitHub remote, so there are no pull requests to show."); return; }
    std::string state = fPullRequests->State();
    // GitHub allows 60 unauthenticated requests an hour: never reload the same list within a minute unless asked.
    if (!force && state == fPullRequestsLoadedState && system_time() - fPullRequestsLoadedAt < 60 * 1000000LL) return;
    fPullRequestsLoadedAt = system_time(); fPullRequestsLoadedState = state;
    auto client = std::shared_ptr<GitHubClient>(GitHub().release());
    int64 request = ++fPullRequestRequest;
    fPullRequests->SetStatus("Loading pull requests" B_UTF8_ELLIPSIS);
    fNetwork->Submit([client, state, request, this](const std::atomic<bool>& cancel) -> AsyncQueue::Callback {
        auto result = client->ListPullRequests(state, &cancel);
        return [=] {
            if (request != fPullRequestRequest) return;
            fPullRequests->SetRepository(fGitHubRepository, !App()->GitHubToken().empty());
            fPullRequestList = result.value;
            fPullRequests->SetPullRequests(result.value, result.error);
            fModel.pullRequests = result.ok() ? static_cast<int>(std::count_if(result.value.begin(), result.value.end(), [](const PullRequest& p) { return p.state == "open"; })) : -1;
        };
    }, "pulls");
}
void RepositoryWindow::LoadPullRequest(int number) {
    auto client = std::shared_ptr<GitHubClient>(GitHub().release());
    int64 request = ++fPullRequestRequest;
    fNetwork->Submit([client, number, request, this](const std::atomic<bool>& cancel) -> AsyncQueue::Callback {
        auto pr = client->GetPullRequest(number, &cancel);
        auto comments = client->Comments(number, &cancel);
        auto reviews = client->Reviews(number, &cancel);
        return [=] {
            if (request != fPullRequestRequest) return;
            if (!pr.ok()) { fPullRequests->SetStatus(pr.error); return; }
            fPullRequests->SetDetails(pr.value, comments.value, reviews.value);
        };
    }, "pull");
}
// ---------------------------------------------------------------- operations
void RepositoryWindow::RunOperation(const std::string& title, Operation operation, std::function<void(const ProcessResult&)> done, bool network) {
    fBusy = true;
    fUndoButton->SetEnabled(false);
    fWorkingCopy->SetBusy(true);
    SetStatus(title + B_UTF8_ELLIPSIS);
    AsyncQueue& queue = network ? *fNetwork : *fOps;
    queue.Submit([operation, title, done, this](const std::atomic<bool>& cancel) -> AsyncQueue::Callback {
        ProcessResult result = operation(cancel);
        return [=] {
            fBusy = false;
            fWorkingCopy->SetBusy(false);
            fUndoButton->SetEnabled(!fUndo.Empty());
            if (!result.ok()) { SetStatus(title + " failed"); ShowError(title.c_str(), Trim(result.diagnostic())); }
            else SetStatus(title + " done");
            if (done) done(result);
            Refresh();
        };
    });
}
void RepositoryWindow::RecordUndo(std::string description, std::function<ProcessResult()> undo) {
    fUndo.Push(std::move(description), std::move(undo));
    fUndoButton->SetEnabled(true);
    fUndoButton->SetToolTip(fUndo.Description().c_str());
}
void RepositoryWindow::Undo() {
    if (fUndo.Empty() || fBusy) return;
    std::string description = fUndo.Description();
    fBusy = true; SetStatus(description + B_UTF8_ELLIPSIS);
    ProcessResult result = fUndo.Pop();   // local Git work; quick enough for the window thread
    fBusy = false;
    fUndoButton->SetEnabled(!fUndo.Empty());
    if (!result.ok()) ShowError("Undo", Trim(result.diagnostic())); else SetStatus(description + " done");
    Refresh();
}
std::vector<std::string> RepositoryWindow::Paths(BMessage* message) const {
    std::vector<std::string> paths;
    const char* path;
    for (int32 i = 0; message->FindString("path", i, &path) == B_OK; ++i) paths.push_back(path);
    return paths;
}
void RepositoryWindow::Prompt(const std::string& title, const char* label, const std::string& initial, BMessage reply, const char* secondLabel,
    const std::string& secondInitial, const char* action, bool multiline, const std::vector<std::string>& choices, const char* choiceLabel) {
    (new PromptWindow(title.c_str(), label, initial, BMessenger(this), reply, secondLabel, secondInitial, action, multiline, choices, choiceLabel))->Show();
}
std::string RepositoryWindow::BranchAt(const std::string& hash) const {
    for (const auto& b : fModel.branches) if (!b.remote && b.hash == hash) return b.name;
    return "";
}
// ---------------------------------------------------------------- message routing
void RepositoryWindow::MessageReceived(BMessage* message) {
    switch (message->what) {
        case kWorkDone: fJobs->Drain(); fOps->Drain(); fNetwork->Drain(); break;
        case kRefresh: Refresh(); if (fCard == kCardPullRequests) LoadPullRequests(true); break;
        case kRefreshTick: if (IsActive() && !fBusy) Refresh(); break;
        case kPreferencesChanged: fRepo->SetOptions(Options()); Refresh(); break;
        case kQuitApp: be_app->PostMessage(B_QUIT_REQUESTED); break;
        case kOpenRepository: case kCloneRepository: case kNewRepository: case kShowPreferences: be_app->PostMessage(message); break;
        case B_ABOUT_REQUESTED: be_app->PostMessage(message); break;
        case kOpenInTracker: {
            auto paths = Paths(message);
            OpenExternally(paths.empty() ? fRoot : fRoot + "/" + paths.front());
            break;
        }
        case kOpenTerminal: {
            ProcessOptions options; options.directory = fRoot; options.timeout = std::chrono::milliseconds(5000);
            RunProcess({"Terminal", "-w", fRoot}, options);
            break;
        }
        case kOpenInEditor: { for (const auto& p : Paths(message)) OpenExternally(fRoot + "/" + p); break; }
        case kViewWorkingCopy: fSidebar->SelectKind(SidebarEntry::kWorkingCopy); break;
        case kViewHistory: fSidebar->SelectKind(SidebarEntry::kHistoryAll); break;
        case kViewPullRequests: if (fModel.hasGitHub) fSidebar->SelectKind(SidebarEntry::kPullRequests); else ShowError("Pull Requests", "This repository has no GitHub remote."); break;
        case kUndo: Undo(); break;
        case kSidebarSelection: {
            const SidebarEntry* entry = fSidebar->Selected();
            if (!entry) break;
            // A rebuilt sidebar re-selects the same entry; that echo must not reload anything.
            if (entry->kind == fSelectedEntry.kind && entry->name == fSelectedEntry.name && entry->kind != SidebarEntry::kSubmodule) break;
            fSelectedEntry = *entry;
            switch (entry->kind) {
                case SidebarEntry::kWorkingCopy: ShowCard(kCardWorkingCopy); break;
                case SidebarEntry::kHistoryAll: fHistoryRevisions = {}; fHistory->SetTitle("History: all branches"); ShowCard(kCardHistory); LoadHistory(true); break;
                case SidebarEntry::kHistoryCurrent: fHistoryRevisions = {"HEAD"}; fHistory->SetTitle("History: " + fModel.state.head); ShowCard(kCardHistory); LoadHistory(true); break;
                case SidebarEntry::kBranch: case SidebarEntry::kRemoteBranch: case SidebarEntry::kTag:
                    fHistoryRevisions = {entry->name}; fHistory->SetTitle("History: " + entry->name); ShowCard(kCardHistory); LoadHistory(true); break;
                case SidebarEntry::kRemote: fHistoryRevisions = {"--remotes=" + entry->name}; fHistory->SetTitle("History: remote " + entry->name); ShowCard(kCardHistory);
                    fHistoryRevisions = {"--glob=refs/remotes/" + entry->name + "/*"}; LoadHistory(true); break;
                case SidebarEntry::kStash: {
                    ShowCard(kCardHistory);
                    fHistory->SetTitle("Stash: " + entry->label);
                    fHistoryRevisions = {entry->hash};
                    LoadHistory(true);
                    Stash stash; for (const auto& s : fModel.stashes) if (s.ref == entry->name) stash = s;
                    auto repo = fRepo; int64 request = ++fCommitRequest;
                    fJobs->Submit([repo, stash, request, this](const std::atomic<bool>& cancel) -> AsyncQueue::Callback {
                        auto files = repo->StashFiles(stash, &cancel);
                        return [=] { if (request == fCommitRequest) fHistory->ShowStash(stash, files.value); };
                    }, "commit");
                    break;
                }
                case SidebarEntry::kPullRequests: ShowCard(kCardPullRequests); LoadPullRequests(); break;
                case SidebarEntry::kSubmodule: OpenExternally(fRoot + "/" + entry->name); break;
                default: break;
            }
            break;
        }
        case kSidebarInvoke: HandleSidebarInvoke(message); break;
        case kSidebarDrop: HandleDrop(message); break;
        case kHistorySelection: {
            const char* hash;
            std::vector<std::string> hashes;
            for (int32 i = 0; message->FindString("hash", i, &hash) == B_OK; ++i) hashes.push_back(hash);
            if (hashes.size() == 1) LoadCommit(hashes.front());
            else if (hashes.size() == 2) {
                auto repo = fRepo; int64 request = ++fCommitRequest;
                std::string from = hashes[1], to = hashes[0];
                fJobs->Submit([repo, from, to, request, this](const std::atomic<bool>& cancel) -> AsyncQueue::Callback {
                    auto files = repo->RangeFiles(from, to, &cancel);
                    return [=] { if (request == fCommitRequest) fHistory->ShowRange(from, to, files.value); };
                }, "commit");
            } else if (hashes.empty()) fHistory->ClearDetails("Select a commit to see its details.");
            else fHistory->ClearDetails(std::to_string(hashes.size()) + " commits selected.");
            break;
        }
        case kHistoryMore: LoadHistory(false); break;
        case kCommitFileSelected: {
            const char *path = "", *hash = "", *from = "", *to = "";
            message->FindString("path", &path); message->FindString("hash", &hash); message->FindString("from", &from); message->FindString("to", &to);
            LoadCommitDiff(hash, from, to, path);
            break;
        }
        case kDiffSelection: {
            const char* path = ""; bool staged = false, untracked = false, conflicted = false;
            message->FindString("path", &path); message->FindBool("staged", &staged); message->FindBool("untracked", &untracked); message->FindBool("conflicted", &conflicted);
            LoadWorkingDiff(path, staged, untracked, conflicted);
            break;
        }
        case kStage: case kUnstage: case kStageAll: case kUnstageAll: case kDiscard: case kDiscardAll: case kCommit: case kAmendToggled:
        case kStageHunk: case kUnstageHunk: case kDiscardHunk: case kStageLines: case kUnstageLines: case kDiscardLines:
        case kResolveOurs: case kResolveTheirs: case kMarkResolved: case kIgnoreFile: case kContinueOperation: case kAbortOperation:
            HandleWorkingCopy(message); break;
        case kCheckoutBranch: case kNewBranch: case kDeleteBranch: case kRenameBranch: case kMergeBranch: case kRebaseBranch:
        case kTrackBranch: case kDeleteRemoteBranch: case kNewTag: case kDeleteTag: case kPushTag: case kApplyStash: case kPopStash:
        case kDropStash: case kSaveStash: case kSetUpstream: case kCompareBranch: case kSquashMerge:
            HandleBranches(message); break;
        case kCheckoutCommit: case kBranchHere: case kTagHere: case kCherryPick: case kRevertCommit: case kResetSoft: case kResetMixed:
        case kResetHard: case kCopyHash: case kCompareCommits:
            HandleHistory(message); break;
        case kFetch: case kFetchAll: case kPull: case kPush: case kPushBranch: case kPullBranch: case kAddRemote: case kRemoveRemote: case kUpdateSubmodules:
            HandleRemote(message); break;
        case kPullRequestsRefresh: case kPullRequestSelected: case kPullRequestCheckout: case kPullRequestMerge: case kPullRequestClose:
        case kPullRequestOpenBrowser: case kNewPullRequest: case kSubmitPullRequest: case kPullRequestComment:
            HandlePullRequests(message); break;
        case B_SIMPLE_DATA: {
            // Files or folders dropped from Tracker: open another repository.
            entry_ref ref;
            if (message->FindRef("refs", &ref) == B_OK) { BMessage forward(B_REFS_RECEIVED); forward.AddRef("refs", &ref); be_app->PostMessage(&forward); }
            break;
        }
        default: BWindow::MessageReceived(message);
    }
}
}
