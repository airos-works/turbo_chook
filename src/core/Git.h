#pragma once
#include "core/Process.h"
#include <atomic>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tc {
// ---------------------------------------------------------------- model
struct FileStatus {
    char index = ' ', worktree = ' ';
    std::string path, originalPath;
    bool untracked() const { return index == '?' && worktree == '?'; }
    bool ignored() const { return index == '!' && worktree == '!'; }
    bool conflicted() const;
    bool staged() const { return !untracked() && !ignored() && !conflicted() && index != ' '; }
    bool unstaged() const { return untracked() || (!conflicted() && worktree != ' '); }
    std::string description(bool stagedSide) const;
};
struct Commit {
    std::string hash;
    std::vector<std::string> parents;
    std::string authorName, authorEmail, authorDate, commitDate, subject, body;
    std::vector<std::string> refs;   // decorated names: HEAD -> main, origin/main, tag: v1
    bool merge() const { return parents.size() > 1; }
    std::string shortHash() const { return hash.substr(0, 8); }
};
struct Branch {
    std::string name;       // main, feature/x, origin/main
    std::string fullRef;    // refs/heads/main, refs/remotes/origin/main
    std::string hash, upstream, subject, date;
    bool current = false, remote = false, gone = false;
    int ahead = 0, behind = 0;
    std::string remoteName() const;  // "origin" for a remote branch
    std::string shortName() const;   // "main" for origin/main
};
struct Tag { std::string name, hash, subject, date; bool annotated = false; };
struct Stash { int index = 0; std::string ref, hash, message, date; };
struct Remote { std::string name, fetchURL, pushURL; };
struct Submodule { std::string path, hash, describe; char state = ' '; };
struct CommitFile { char status = 'M'; std::string path, originalPath; int added = 0, deleted = 0; bool binary = false; };
struct ReflogEntry { std::string hash, selector, action; };
struct RepositoryState {
    // Which multi-step operation is in progress, if any.
    bool merging = false, rebasing = false, cherryPicking = false, reverting = false, bisecting = false;
    bool detached = false;
    std::string head;         // branch name or short hash when detached
    std::string headHash;
    std::string description() const;
    bool inProgress() const { return merging || rebasing || cherryPicking || reverting || bisecting; }
};
template <typename T> struct GitResult {
    T value{};
    std::string error;
    bool ok() const { return error.empty(); }
};

// ---------------------------------------------------------------- parsers
std::vector<FileStatus> ParseStatus(std::string_view data);
std::vector<Commit> ParseLog(std::string_view data);
std::vector<Branch> ParseBranches(std::string_view data);
std::vector<Tag> ParseTags(std::string_view data);
std::vector<Stash> ParseStashes(std::string_view data);
std::vector<Remote> ParseRemotes(std::string_view data);
std::vector<Submodule> ParseSubmodules(std::string_view data);
std::vector<CommitFile> ParseNameStatus(std::string_view data);
void MergeNumstat(std::vector<CommitFile>& files, std::string_view numstat);
std::vector<ReflogEntry> ParseReflog(std::string_view data);
bool ValidHash(std::string_view hash);
bool ValidRefName(std::string_view name);
std::optional<std::string> GitHubRepository(std::string remoteURL);   // "owner/name"
std::string Trim(std::string text);

// ---------------------------------------------------------------- repository
struct GitOptions {
    std::string executable;          // empty: "git" from PATH
    std::string gitHubToken;         // used for github.com HTTPS remotes
    std::chrono::milliseconds networkTimeout{180000};
};
class GitRepository {
public:
    GitRepository(std::string directory, GitOptions options = {});
    const std::string& Directory() const { return fDirectory; }
    const GitOptions& Options() const { return fOptions; }
    void SetOptions(GitOptions options) { fOptions = std::move(options); }

    ProcessResult Run(std::vector<std::string> arguments, Cancel cancel = nullptr, std::string input = {},
        size_t limit = 64 * 1024 * 1024, std::chrono::milliseconds timeout = std::chrono::milliseconds(60000)) const;
    ProcessResult RunNetwork(std::vector<std::string> arguments, Cancel cancel = nullptr) const;

    // Inspection
    std::string Root() const;
    bool IsRepository() const { return !Root().empty(); }
    GitResult<RepositoryState> State() const;
    GitResult<std::vector<FileStatus>> Status(Cancel cancel = nullptr) const;
    GitResult<std::vector<Commit>> Log(const std::vector<std::string>& revisions, size_t skip, size_t count,
        const std::string& path = {}, Cancel cancel = nullptr) const;
    GitResult<Commit> Show(const std::string& hash) const;
    GitResult<std::vector<Branch>> Branches(Cancel cancel = nullptr) const;
    GitResult<std::vector<Tag>> Tags(Cancel cancel = nullptr) const;
    GitResult<std::vector<Stash>> Stashes(Cancel cancel = nullptr) const;
    GitResult<std::vector<Remote>> Remotes() const;
    GitResult<std::vector<Submodule>> Submodules() const;
    GitResult<std::vector<CommitFile>> CommitFiles(const std::string& hash, Cancel cancel = nullptr) const;
    GitResult<std::vector<CommitFile>> RangeFiles(const std::string& from, const std::string& to, Cancel cancel = nullptr) const;
    GitResult<std::vector<CommitFile>> StashFiles(const Stash& stash, Cancel cancel = nullptr) const;
    GitResult<std::vector<ReflogEntry>> Reflog(size_t count = 30) const;
    ProcessResult Config(const std::string& key, bool global = false) const;
    ProcessResult SetConfig(const std::string& key, const std::string& value, bool global = false) const;
    std::string CurrentBranch() const;
    std::string UpstreamOf(const std::string& branch) const;
    std::string MergeBase(const std::string& a, const std::string& b) const;
    ProcessResult CountAheadBehind(const std::string& a, const std::string& b) const;   // "ahead\tbehind"
    std::string ConflictMarkers(const std::string& path) const;

    // Diffs (unified text)
    ProcessResult WorkingDiff(const std::string& path, bool staged, Cancel cancel = nullptr, int context = 3) const;
    ProcessResult UntrackedDiff(const std::string& path, Cancel cancel = nullptr) const;
    ProcessResult CommitDiff(const std::string& hash, const std::string& path, Cancel cancel = nullptr, int context = 3) const;
    ProcessResult RangeDiff(const std::string& from, const std::string& to, const std::string& path, Cancel cancel = nullptr) const;
    ProcessResult StashDiff(const Stash& stash, const std::string& path, Cancel cancel = nullptr) const;
    ProcessResult FileAt(const std::string& revision, const std::string& path, Cancel cancel = nullptr) const;

    // Working copy
    ProcessResult Stage(const std::vector<std::string>& paths) const;
    ProcessResult StageAll() const;
    ProcessResult Unstage(const std::vector<std::string>& paths) const;
    ProcessResult UnstageAll() const;
    ProcessResult Discard(const std::vector<FileStatus>& files) const;
    ProcessResult ApplyPatch(const std::string& patch, bool cached, bool reverse) const;
    ProcessResult CommitIndex(const std::string& message, bool amend, bool allowEmpty = false) const;
    std::string CreateBackup() const;   // "git stash create": a dangling commit capturing index + worktree
    ProcessResult RestoreBackup(const std::string& hash) const;
    ProcessResult ResolveWith(const std::string& path, bool ours) const;
    ProcessResult MarkResolved(const std::vector<std::string>& paths) const;

    // Branches and tags
    ProcessResult Checkout(const std::string& ref) const;
    ProcessResult CheckoutRemoteBranch(const Branch& remote, const std::string& localName) const;
    ProcessResult CreateBranch(const std::string& name, const std::string& start, bool checkout) const;
    ProcessResult DeleteBranch(const std::string& name, bool force) const;
    ProcessResult DeleteRemoteBranch(const Branch& remote) const;
    ProcessResult RenameBranch(const std::string& from, const std::string& to) const;
    ProcessResult SetUpstream(const std::string& branch, const std::string& upstream) const;
    ProcessResult CreateTag(const std::string& name, const std::string& target, const std::string& message) const;
    ProcessResult DeleteTag(const std::string& name) const;
    ProcessResult PushTag(const std::string& remote, const std::string& name) const;

    // Integration
    ProcessResult Merge(const std::string& ref, bool noFastForward, bool squash = false) const;
    ProcessResult Rebase(const std::string& onto, Cancel cancel = nullptr) const;
    ProcessResult CherryPick(const std::vector<std::string>& hashes) const;
    ProcessResult Revert(const std::string& hash) const;
    ProcessResult Reset(const std::string& ref, const std::string& mode) const;   // soft, mixed, hard
    ProcessResult Abort() const;      // whichever of merge/rebase/cherry-pick/revert is in progress
    ProcessResult Continue() const;

    // Remotes
    ProcessResult Fetch(const std::string& remote, bool prune, Cancel cancel = nullptr) const;
    ProcessResult FetchAll(bool prune, Cancel cancel = nullptr) const;
    ProcessResult Pull(const std::string& remote, const std::string& branch, bool rebase, Cancel cancel = nullptr) const;
    ProcessResult Push(const std::string& remote, const std::string& branch, bool setUpstream, bool force, Cancel cancel = nullptr) const;
    ProcessResult FetchPullRequest(const std::string& remote, int number, const std::string& localBranch, Cancel cancel = nullptr) const;
    ProcessResult AddRemote(const std::string& name, const std::string& url) const;
    ProcessResult RemoveRemote(const std::string& name) const;
    ProcessResult SubmoduleUpdate(Cancel cancel = nullptr) const;

    // Stashes
    ProcessResult StashSave(const std::string& message, bool includeUntracked, bool keepIndex) const;
    ProcessResult StashApply(const Stash& stash, bool pop) const;
    ProcessResult StashDrop(const Stash& stash) const;

    // Whole repositories
    static ProcessResult Clone(const std::string& url, const std::string& destination, const GitOptions& options, Cancel cancel = nullptr);
    static ProcessResult Init(const std::string& directory, const GitOptions& options);
private:
    std::vector<std::string> BaseArguments() const;
    std::string fDirectory;
    GitOptions fOptions;
};
std::string Base64(std::string_view data);
}
