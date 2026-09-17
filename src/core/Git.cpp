#include "core/Git.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace tc {
namespace {
constexpr char kUnit = '\x1f';
std::string_view Field(std::string_view data, size_t& offset, char separator) {
    if (offset >= data.size()) { offset = data.size() + 1; return {}; }
    auto end = data.find(separator, offset);
    if (end == std::string_view::npos) end = data.size();
    auto result = data.substr(offset, end - offset);
    offset = end + 1;
    return result;
}
std::vector<std::string_view> Split(std::string_view data, char separator) {
    std::vector<std::string_view> parts;
    size_t offset = 0;
    while (offset <= data.size()) {
        auto end = data.find(separator, offset);
        if (end == std::string_view::npos) end = data.size();
        parts.push_back(data.substr(offset, end - offset));
        offset = end + 1;
    }
    return parts;
}
ProcessResult Failure(std::string error) { ProcessResult r; r.error = std::move(error); return r; }
bool Exists(const std::string& path) { struct stat st; return stat(path.c_str(), &st) == 0; }
std::string ReadFile(const std::string& path) {
    std::ifstream in(path);
    std::stringstream buffer; buffer << in.rdbuf(); return buffer.str();
}
template <typename T> GitResult<T> Fail(std::string error) { GitResult<T> r; r.error = std::move(error); return r; }
}
std::string Trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    return s.substr(start);
}
bool ValidHash(std::string_view hash) {
    return hash.size() >= 4 && hash.size() <= 64
        && std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isxdigit(c); });
}
bool ValidRefName(std::string_view name) {
    if (name.empty() || name.front() == '-' || name.front() == '/' || name.back() == '/' || name.back() == '.') return false;
    if (name.find("..") != std::string_view::npos || name.find("@{") != std::string_view::npos || name == "@") return false;
    if (name.find("//") != std::string_view::npos || name.find("/.") != std::string_view::npos) return false;
    if (name.size() > 5 && name.substr(name.size() - 5) == ".lock") return false;
    for (unsigned char c : name)
        if (c < 32 || c == 127 || c == ' ' || c == '~' || c == '^' || c == ':' || c == '?' || c == '*' || c == '[' || c == '\\')
            return false;
    return true;
}
std::string Base64(std::string_view data) {
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    while (i + 2 < data.size()) {
        unsigned v = (static_cast<unsigned char>(data[i]) << 16) | (static_cast<unsigned char>(data[i+1]) << 8) | static_cast<unsigned char>(data[i+2]);
        out += table[v >> 18]; out += table[(v >> 12) & 63]; out += table[(v >> 6) & 63]; out += table[v & 63];
        i += 3;
    }
    if (i + 1 == data.size()) {
        unsigned v = static_cast<unsigned char>(data[i]) << 16;
        out += table[v >> 18]; out += table[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == data.size()) {
        unsigned v = (static_cast<unsigned char>(data[i]) << 16) | (static_cast<unsigned char>(data[i+1]) << 8);
        out += table[v >> 18]; out += table[(v >> 12) & 63]; out += table[(v >> 6) & 63]; out += '=';
    }
    return out;
}
std::optional<std::string> GitHubRepository(std::string remote) {
    remote = Trim(remote);
    std::string path;
    for (const char* prefix : {"git@github.com:", "https://github.com/", "http://github.com/",
            "ssh://git@github.com/", "ssh://git@github.com:22/", "git://github.com/"}) {
        if (remote.rfind(prefix, 0) == 0) { path = remote.substr(std::strlen(prefix)); break; }
    }
    if (path.empty()) return {};
    while (!path.empty() && path.back() == '/') path.pop_back();
    if (path.size() > 4 && path.substr(path.size() - 4) == ".git") path.resize(path.size() - 4);
    auto slash = path.find('/');
    if (slash == 0 || slash == std::string::npos || slash == path.size() - 1 || path.find('/', slash + 1) != std::string::npos) return {};
    for (unsigned char c : path) if (!std::isalnum(c) && c != '/' && c != '.' && c != '-' && c != '_') return {};
    return path;
}
// ---------------------------------------------------------------- model helpers
bool FileStatus::conflicted() const {
    return (index == 'U' || worktree == 'U') || (index == 'A' && worktree == 'A') || (index == 'D' && worktree == 'D');
}
std::string FileStatus::description(bool stagedSide) const {
    if (conflicted()) {
        if (index == 'D' && worktree == 'D') return "deleted by both";
        if (index == 'A' && worktree == 'A') return "added by both";
        if (index == 'U' && worktree == 'D') return "deleted by them";
        if (index == 'D' && worktree == 'U') return "deleted by us";
        if (index == 'A' && worktree == 'U') return "added by us";
        if (index == 'U' && worktree == 'A') return "added by them";
        return "conflict";
    }
    if (untracked()) return "untracked";
    switch (stagedSide ? index : worktree) {
        case 'M': return "modified";
        case 'A': return "added";
        case 'D': return "deleted";
        case 'R': return "renamed";
        case 'C': return "copied";
        case 'T': return "type changed";
        default: return "";
    }
}
std::string Branch::remoteName() const {
    if (!remote) return "";
    auto slash = name.find('/');
    return slash == std::string::npos ? name : name.substr(0, slash);
}
std::string Branch::shortName() const {
    if (!remote) return name;
    auto slash = name.find('/');
    return slash == std::string::npos ? name : name.substr(slash + 1);
}
std::string RepositoryState::description() const {
    if (rebasing) return "Rebase in progress";
    if (merging) return "Merge in progress";
    if (cherryPicking) return "Cherry-pick in progress";
    if (reverting) return "Revert in progress";
    if (bisecting) return "Bisect in progress";
    return "";
}
// ---------------------------------------------------------------- parsers
std::vector<FileStatus> ParseStatus(std::string_view data) {
    std::vector<FileStatus> files;
    size_t offset = 0;
    while (offset < data.size()) {
        auto record = Field(data, offset, '\0');
        if (record.size() < 4 || record[2] != ' ') continue;
        FileStatus f{record[0], record[1], std::string(record.substr(3)), {}};
        if (f.index == 'R' || f.index == 'C' || f.worktree == 'R' || f.worktree == 'C')
            f.originalPath = std::string(Field(data, offset, '\0'));
        files.push_back(std::move(f));
    }
    return files;
}
std::vector<Commit> ParseLog(std::string_view data) {
    std::vector<Commit> commits;
    size_t offset = 0;
    while (offset < data.size()) {
        auto record = Field(data, offset, '\0');
        if (record.empty()) continue;
        auto fields = Split(record, kUnit);
        if (fields.size() < 9 || !ValidHash(fields[0])) continue;
        Commit c;
        c.hash = std::string(fields[0]);
        for (auto parent : Split(fields[1], ' ')) if (!parent.empty()) c.parents.emplace_back(parent);
        c.authorName = std::string(fields[2]);
        c.authorEmail = std::string(fields[3]);
        c.authorDate = std::string(fields[4]);
        c.commitDate = std::string(fields[5]);
        c.subject = std::string(fields[6]);
        for (auto ref : Split(fields[7], ',')) { auto r = Trim(std::string(ref)); if (!r.empty()) c.refs.push_back(r); }
        c.body = Trim(std::string(fields[8]));
        commits.push_back(std::move(c));
    }
    return commits;
}
std::vector<Branch> ParseBranches(std::string_view data) {
    std::vector<Branch> branches;
    for (auto line : Split(data, '\n')) {
        auto fields = Split(line, kUnit);
        if (fields.size() < 7) continue;
        Branch b;
        b.fullRef = std::string(fields[0]);
        b.hash = std::string(fields[1]);
        b.upstream = std::string(fields[2]);
        std::string track(fields[3]);
        b.current = fields[4] == "*";
        b.subject = std::string(fields[5]);
        b.date = std::string(fields[6]);
        if (b.fullRef.rfind("refs/heads/", 0) == 0) b.name = b.fullRef.substr(11);
        else if (b.fullRef.rfind("refs/remotes/", 0) == 0) { b.name = b.fullRef.substr(13); b.remote = true; }
        else continue;
        if (b.remote && b.shortName() == "HEAD") continue;
        b.gone = track.find("gone") != std::string::npos;
        auto ahead = track.find("ahead ");
        if (ahead != std::string::npos) b.ahead = std::atoi(track.c_str() + ahead + 6);
        auto behind = track.find("behind ");
        if (behind != std::string::npos) b.behind = std::atoi(track.c_str() + behind + 7);
        branches.push_back(std::move(b));
    }
    return branches;
}
std::vector<Tag> ParseTags(std::string_view data) {
    std::vector<Tag> tags;
    for (auto line : Split(data, '\n')) {
        auto fields = Split(line, kUnit);
        if (fields.size() < 6) continue;
        Tag t;
        t.name = std::string(fields[0]);
        if (t.name.rfind("refs/tags/", 0) == 0) t.name = t.name.substr(10);
        t.annotated = fields[1] == "tag";
        t.hash = std::string(t.annotated && !fields[3].empty() ? fields[3] : fields[2]);
        t.subject = std::string(fields[4]);
        t.date = std::string(fields[5]);
        tags.push_back(std::move(t));
    }
    return tags;
}
std::vector<Stash> ParseStashes(std::string_view data) {
    std::vector<Stash> stashes;
    for (auto line : Split(data, '\n')) {
        auto fields = Split(line, kUnit);
        if (fields.size() < 4) continue;
        Stash s;
        s.ref = std::string(fields[0]);
        auto open = s.ref.find('{');
        s.index = open == std::string::npos ? static_cast<int>(stashes.size()) : std::atoi(s.ref.c_str() + open + 1);
        s.hash = std::string(fields[1]);
        s.message = std::string(fields[2]);
        s.date = std::string(fields[3]);
        stashes.push_back(std::move(s));
    }
    return stashes;
}
std::vector<Remote> ParseRemotes(std::string_view data) {
    std::vector<Remote> remotes;
    for (auto line : Split(data, '\n')) {
        auto tab = line.find('\t');
        if (tab == std::string_view::npos) continue;
        std::string name(line.substr(0, tab));
        auto rest = line.substr(tab + 1);
        auto space = rest.rfind(' ');
        std::string url(rest.substr(0, space)), kind(space == std::string_view::npos ? "" : rest.substr(space + 1));
        auto found = std::find_if(remotes.begin(), remotes.end(), [&](const Remote& r) { return r.name == name; });
        if (found == remotes.end()) { remotes.push_back({name, {}, {}}); found = remotes.end() - 1; }
        if (kind == "(push)") found->pushURL = url; else found->fetchURL = url;
    }
    return remotes;
}
std::vector<Submodule> ParseSubmodules(std::string_view data) {
    std::vector<Submodule> modules;
    for (auto line : Split(data, '\n')) {
        if (line.size() < 42) continue;
        Submodule m;
        m.state = line[0] == ' ' ? ' ' : line[0];
        auto rest = line.substr(1);
        auto space = rest.find(' ');
        if (space == std::string_view::npos) continue;
        m.hash = std::string(rest.substr(0, space));
        rest = rest.substr(space + 1);
        auto paren = rest.find(" (");
        m.path = std::string(paren == std::string_view::npos ? rest : rest.substr(0, paren));
        if (paren != std::string_view::npos && rest.back() == ')') m.describe = std::string(rest.substr(paren + 2, rest.size() - paren - 3));
        modules.push_back(std::move(m));
    }
    return modules;
}
std::vector<CommitFile> ParseNameStatus(std::string_view data) {
    std::vector<CommitFile> files;
    size_t offset = 0;
    while (offset < data.size()) {
        auto status = Field(data, offset, '\0');
        if (status.empty()) continue;
        CommitFile f;
        f.status = status[0];
        if (f.status == 'R' || f.status == 'C') {
            f.originalPath = std::string(Field(data, offset, '\0'));
            f.path = std::string(Field(data, offset, '\0'));
        } else {
            f.path = std::string(Field(data, offset, '\0'));
        }
        if (!f.path.empty()) files.push_back(std::move(f));
    }
    return files;
}
void MergeNumstat(std::vector<CommitFile>& files, std::string_view data) {
    size_t offset = 0;
    while (offset < data.size()) {
        auto record = Field(data, offset, '\0');
        if (record.empty()) continue;
        auto tab1 = record.find('\t');
        auto tab2 = record.find('\t', tab1 + 1);
        if (tab1 == std::string_view::npos || tab2 == std::string_view::npos) continue;
        std::string added(record.substr(0, tab1)), deleted(record.substr(tab1 + 1, tab2 - tab1 - 1));
        std::string path(record.substr(tab2 + 1));
        if (path.empty()) {   // rename: "added\tdeleted\t\0old\0new\0"
            Field(data, offset, '\0');
            path = std::string(Field(data, offset, '\0'));
        }
        for (auto& f : files) {
            if (f.path != path) continue;
            f.binary = added == "-";
            f.added = f.binary ? 0 : std::atoi(added.c_str());
            f.deleted = f.binary ? 0 : std::atoi(deleted.c_str());
        }
    }
}
std::vector<ReflogEntry> ParseReflog(std::string_view data) {
    std::vector<ReflogEntry> entries;
    for (auto line : Split(data, '\n')) {
        auto fields = Split(line, kUnit);
        if (fields.size() < 3 || !ValidHash(fields[0])) continue;
        entries.push_back({std::string(fields[0]), std::string(fields[1]), std::string(fields[2])});
    }
    return entries;
}
// ---------------------------------------------------------------- repository
GitRepository::GitRepository(std::string directory, GitOptions options)
    : fDirectory(std::move(directory)), fOptions(std::move(options)) {}
std::vector<std::string> GitRepository::BaseArguments() const {
    std::vector<std::string> args{"git", "--no-pager", "-c", "color.ui=false", "-c", "core.quotepath=false",
        "-c", "advice.detachedHead=false", "-c", "log.showSignature=false"};
    if (!fOptions.gitHubToken.empty()) {
        args.push_back("-c");
        args.push_back("http.https://github.com/.extraheader=AUTHORIZATION: basic "
            + Base64("x-access-token:" + fOptions.gitHubToken));
    }
    return args;
}
ProcessResult GitRepository::Run(std::vector<std::string> arguments, Cancel cancel, std::string input,
    size_t limit, std::chrono::milliseconds timeout) const {
    auto args = BaseArguments();
    args.insert(args.end(), arguments.begin(), arguments.end());
    ProcessOptions options;
    options.directory = fDirectory; options.cancel = cancel; options.input = std::move(input);
    options.outputLimit = limit; options.timeout = timeout; options.executable = fOptions.executable;
    options.environment = {"GIT_TERMINAL_PROMPT=0", "GIT_LITERAL_PATHSPECS=1", "LC_ALL=C", "GIT_OPTIONAL_LOCKS=0",
        "GIT_EDITOR=true", "GIT_SEQUENCE_EDITOR=true", "GIT_MERGE_AUTOEDIT=no", "GIT_SSH_COMMAND=ssh -o BatchMode=yes"};
    return RunProcess(args, options);
}
ProcessResult GitRepository::RunNetwork(std::vector<std::string> arguments, Cancel cancel) const {
    return Run(std::move(arguments), cancel, {}, 16 * 1024 * 1024, fOptions.networkTimeout);
}
std::string GitRepository::Root() const {
    auto result = Run({"rev-parse", "--show-toplevel"});
    return result.ok() ? Trim(result.output) : "";
}
GitResult<RepositoryState> GitRepository::State() const {
    GitResult<RepositoryState> result;
    auto gitDir = Run({"rev-parse", "--absolute-git-dir"});
    if (!gitDir.ok()) return Fail<RepositoryState>(gitDir.diagnostic());
    auto dir = Trim(gitDir.output);
    result.value.merging = Exists(dir + "/MERGE_HEAD");
    result.value.rebasing = Exists(dir + "/rebase-merge") || Exists(dir + "/rebase-apply");
    result.value.cherryPicking = Exists(dir + "/CHERRY_PICK_HEAD");
    result.value.reverting = Exists(dir + "/REVERT_HEAD");
    result.value.bisecting = Exists(dir + "/BISECT_LOG");
    auto head = Run({"symbolic-ref", "--short", "-q", "HEAD"});
    auto hash = Run({"rev-parse", "--verify", "-q", "HEAD"});
    result.value.headHash = hash.ok() ? Trim(hash.output) : "";
    if (head.ok()) result.value.head = Trim(head.output);
    else { result.value.detached = true; result.value.head = result.value.headHash.substr(0, 8); }
    return result;
}
GitResult<std::vector<FileStatus>> GitRepository::Status(Cancel cancel) const {
    auto r = Run({"status", "--porcelain=v1", "-z", "--untracked-files=all", "--no-renames"}, cancel);
    if (!r.ok()) return Fail<std::vector<FileStatus>>(r.diagnostic());
    GitResult<std::vector<FileStatus>> result; result.value = ParseStatus(r.output); return result;
}
static const char* kLogFormat = "--format=%H\x1f%P\x1f%an\x1f%ae\x1f%aI\x1f%cI\x1f%s\x1f%D\x1f%b";
GitResult<std::vector<Commit>> GitRepository::Log(const std::vector<std::string>& revisions, size_t skip, size_t count,
    const std::string& path, Cancel cancel) const {
    std::vector<std::string> args{"log", "-z", "--topo-order", "--date=iso-strict", std::string(kLogFormat),
        "--skip=" + std::to_string(skip), "-n", std::to_string(count)};
    if (revisions.empty()) { args.push_back("--branches"); args.push_back("--tags"); args.push_back("--remotes"); args.push_back("HEAD"); }
    else for (const auto& rev : revisions) { if (!rev.empty() && rev.front() != '-') args.push_back(rev); }
    if (!path.empty()) { args.push_back("--"); args.push_back(path); }
    auto r = Run(args, cancel);
    if (!r.ok()) return Fail<std::vector<Commit>>(r.diagnostic());
    GitResult<std::vector<Commit>> result; result.value = ParseLog(r.output); return result;
}
GitResult<Commit> GitRepository::Show(const std::string& hash) const {
    if (!ValidHash(hash) && !ValidRefName(hash)) return Fail<Commit>("Invalid revision.");
    auto r = Run({"log", "-z", "-n", "1", "--date=iso-strict", std::string(kLogFormat), hash, "--"});
    if (!r.ok()) return Fail<Commit>(r.diagnostic());
    auto commits = ParseLog(r.output);
    if (commits.empty()) return Fail<Commit>("Commit not found.");
    GitResult<Commit> result; result.value = commits.front(); return result;
}
GitResult<std::vector<Branch>> GitRepository::Branches(Cancel cancel) const {
    auto r = Run({"for-each-ref", "--sort=refname",
        "--format=%(refname)\x1f%(objectname)\x1f%(upstream:short)\x1f%(upstream:track)\x1f%(HEAD)\x1f%(subject)\x1f%(committerdate:iso-strict)",
        "refs/heads", "refs/remotes"}, cancel);
    if (!r.ok()) return Fail<std::vector<Branch>>(r.diagnostic());
    GitResult<std::vector<Branch>> result; result.value = ParseBranches(r.output); return result;
}
GitResult<std::vector<Tag>> GitRepository::Tags(Cancel cancel) const {
    auto r = Run({"for-each-ref", "--sort=-creatordate",
        "--format=%(refname)\x1f%(objecttype)\x1f%(objectname)\x1f%(*objectname)\x1f%(subject)\x1f%(creatordate:iso-strict)",
        "refs/tags"}, cancel);
    if (!r.ok()) return Fail<std::vector<Tag>>(r.diagnostic());
    GitResult<std::vector<Tag>> result; result.value = ParseTags(r.output); return result;
}
GitResult<std::vector<Stash>> GitRepository::Stashes(Cancel cancel) const {
    auto r = Run({"stash", "list", "--format=%gd\x1f%H\x1f%gs\x1f%ci"}, cancel);
    if (!r.ok()) return Fail<std::vector<Stash>>(r.diagnostic());
    GitResult<std::vector<Stash>> result; result.value = ParseStashes(r.output); return result;
}
GitResult<std::vector<Remote>> GitRepository::Remotes() const {
    auto r = Run({"remote", "-v"});
    if (!r.ok()) return Fail<std::vector<Remote>>(r.diagnostic());
    GitResult<std::vector<Remote>> result; result.value = ParseRemotes(r.output); return result;
}
GitResult<std::vector<Submodule>> GitRepository::Submodules() const {
    if (!Exists(Root() + "/.gitmodules")) return {};
    auto r = Run({"submodule", "status"});
    if (!r.ok()) return Fail<std::vector<Submodule>>(r.diagnostic());
    GitResult<std::vector<Submodule>> result; result.value = ParseSubmodules(r.output); return result;
}
GitResult<std::vector<CommitFile>> GitRepository::CommitFiles(const std::string& hash, Cancel cancel) const {
    if (!ValidHash(hash)) return Fail<std::vector<CommitFile>>("Invalid commit identifier.");
    auto names = Run({"show", "--format=", "--name-status", "-z", "-M", "--root", "--diff-merges=first-parent", hash, "--"}, cancel);
    if (!names.ok()) return Fail<std::vector<CommitFile>>(names.diagnostic());
    GitResult<std::vector<CommitFile>> result;
    result.value = ParseNameStatus(names.output);
    auto numbers = Run({"show", "--format=", "--numstat", "-z", "-M", "--root", "--diff-merges=first-parent", hash, "--"}, cancel);
    if (numbers.ok()) MergeNumstat(result.value, numbers.output);
    return result;
}
GitResult<std::vector<CommitFile>> GitRepository::RangeFiles(const std::string& from, const std::string& to, Cancel cancel) const {
    auto names = Run({"diff", "--name-status", "-z", "-M", from, to, "--"}, cancel);
    if (!names.ok()) return Fail<std::vector<CommitFile>>(names.diagnostic());
    GitResult<std::vector<CommitFile>> result;
    result.value = ParseNameStatus(names.output);
    auto numbers = Run({"diff", "--numstat", "-z", "-M", from, to, "--"}, cancel);
    if (numbers.ok()) MergeNumstat(result.value, numbers.output);
    return result;
}
GitResult<std::vector<CommitFile>> GitRepository::StashFiles(const Stash& stash, Cancel cancel) const {
    return RangeFiles(stash.hash + "^", stash.hash, cancel);
}
GitResult<std::vector<ReflogEntry>> GitRepository::Reflog(size_t count) const {
    auto r = Run({"reflog", "show", "-n", std::to_string(count), "--format=%H\x1f%gd\x1f%gs"});
    if (!r.ok()) return Fail<std::vector<ReflogEntry>>(r.diagnostic());
    GitResult<std::vector<ReflogEntry>> result; result.value = ParseReflog(r.output); return result;
}
ProcessResult GitRepository::Config(const std::string& key, bool global) const {
    std::vector<std::string> args{"config"};
    if (global) args.push_back("--global");
    args.push_back("--get"); args.push_back(key);
    return Run(args);
}
ProcessResult GitRepository::SetConfig(const std::string& key, const std::string& value, bool global) const {
    std::vector<std::string> args{"config"};
    if (global) args.push_back("--global");
    args.push_back(key); args.push_back(value);
    return Run(args);
}
std::string GitRepository::CurrentBranch() const {
    auto r = Run({"symbolic-ref", "--short", "-q", "HEAD"});
    return r.ok() ? Trim(r.output) : "";
}
std::string GitRepository::UpstreamOf(const std::string& branch) const {
    auto r = Run({"rev-parse", "--abbrev-ref", "--symbolic-full-name", branch + "@{upstream}"});
    return r.ok() ? Trim(r.output) : "";
}
std::string GitRepository::MergeBase(const std::string& a, const std::string& b) const {
    auto r = Run({"merge-base", a, b});
    return r.ok() ? Trim(r.output) : "";
}
ProcessResult GitRepository::CountAheadBehind(const std::string& a, const std::string& b) const {
    return Run({"rev-list", "--left-right", "--count", a + "..." + b});
}
std::string GitRepository::ConflictMarkers(const std::string& path) const {
    return ReadFile(Root() + "/" + path);
}
// ---------------------------------------------------------------- diffs
ProcessResult GitRepository::WorkingDiff(const std::string& path, bool staged, Cancel cancel, int context) const {
    std::vector<std::string> args{"diff", "--no-ext-diff", "--no-textconv", "--find-renames", "--unified=" + std::to_string(context)};
    if (staged) args.push_back("--cached");
    args.push_back("--"); if (!path.empty()) args.push_back(path);
    return Run(args, cancel);
}
ProcessResult GitRepository::UntrackedDiff(const std::string& path, Cancel cancel) const {
    return Run({"diff", "--no-ext-diff", "--no-index", "--unified=3", "--", "/dev/null", path}, cancel);
}
ProcessResult GitRepository::CommitDiff(const std::string& hash, const std::string& path, Cancel cancel, int context) const {
    if (!ValidHash(hash)) return Failure("Invalid commit identifier.");
    std::vector<std::string> args{"show", "--format=", "--patch", "--root", "--diff-merges=first-parent", "--no-ext-diff",
        "--no-textconv", "--find-renames", "--unified=" + std::to_string(context), hash, "--"};
    if (!path.empty()) args.push_back(path);
    return Run(args, cancel);
}
ProcessResult GitRepository::RangeDiff(const std::string& from, const std::string& to, const std::string& path, Cancel cancel) const {
    std::vector<std::string> args{"diff", "--no-ext-diff", "--no-textconv", "--find-renames", "--unified=3", from, to, "--"};
    if (!path.empty()) args.push_back(path);
    return Run(args, cancel);
}
ProcessResult GitRepository::StashDiff(const Stash& stash, const std::string& path, Cancel cancel) const {
    return RangeDiff(stash.hash + "^", stash.hash, path, cancel);
}
ProcessResult GitRepository::FileAt(const std::string& revision, const std::string& path, Cancel cancel) const {
    return Run({"show", revision + ":" + path}, cancel);
}
// ---------------------------------------------------------------- working copy
ProcessResult GitRepository::Stage(const std::vector<std::string>& paths) const {
    if (paths.empty()) return Failure("Select a file to stage.");
    std::vector<std::string> args{"add", "-A", "--"}; args.insert(args.end(), paths.begin(), paths.end()); return Run(args);
}
ProcessResult GitRepository::StageAll() const { return Run({"add", "-A", "--", "."}); }
ProcessResult GitRepository::Unstage(const std::vector<std::string>& paths) const {
    if (paths.empty()) return Failure("Select a file to unstage.");
    const bool hasHead = Run({"rev-parse", "--verify", "-q", "HEAD"}).ok();
    std::vector<std::string> args = hasHead ? std::vector<std::string>{"reset", "--quiet", "HEAD", "--"}
        : std::vector<std::string>{"rm", "--cached", "-r", "--quiet", "--"};
    args.insert(args.end(), paths.begin(), paths.end()); return Run(args);
}
ProcessResult GitRepository::UnstageAll() const {
    const bool hasHead = Run({"rev-parse", "--verify", "-q", "HEAD"}).ok();
    return hasHead ? Run({"reset", "--quiet", "HEAD", "--", "."}) : Run({"rm", "--cached", "-r", "--quiet", "--", "."});
}
ProcessResult GitRepository::Discard(const std::vector<FileStatus>& files) const {
    std::vector<std::string> tracked, untracked;
    for (const auto& f : files) (f.untracked() ? untracked : tracked).push_back(f.path);
    ProcessResult result;
    if (!tracked.empty()) {
        std::vector<std::string> args{"checkout", "--", }; args.insert(args.end(), tracked.begin(), tracked.end());
        result = Run(args);
        if (!result.ok()) return result;
    }
    if (!untracked.empty()) {
        std::vector<std::string> args{"clean", "-f", "-q", "--"}; args.insert(args.end(), untracked.begin(), untracked.end());
        result = Run(args);
    }
    return result;
}
ProcessResult GitRepository::ApplyPatch(const std::string& patch, bool cached, bool reverse) const {
    std::vector<std::string> args{"apply", "--whitespace=nowarn", "--unidiff-zero"};
    if (cached) args.push_back("--cached");
    if (reverse) args.push_back("--reverse");
    args.push_back("-");
    return Run(args, nullptr, patch);
}
ProcessResult GitRepository::CommitIndex(const std::string& message, bool amend, bool allowEmpty) const {
    if (message.find_first_not_of(" \n\r\t") == std::string::npos) return Failure("Enter a commit message.");
    std::vector<std::string> args{"commit", "--file=-", "--cleanup=strip"};
    if (amend) args.push_back("--amend");
    if (allowEmpty) args.push_back("--allow-empty");
    return Run(args, nullptr, message);
}
std::string GitRepository::CreateBackup() const {
    auto r = Run({"stash", "create"});
    auto hash = r.ok() ? Trim(r.output) : "";
    return ValidHash(hash) ? hash : "";
}
ProcessResult GitRepository::RestoreBackup(const std::string& hash) const {
    if (!ValidHash(hash)) return Failure("No backup to restore.");
    return Run({"stash", "apply", "--index", hash});
}
ProcessResult GitRepository::ResolveWith(const std::string& path, bool ours) const {
    auto r = Run({"checkout", ours ? "--ours" : "--theirs", "--", path});
    if (!r.ok()) return r;
    return Run({"add", "--", path});
}
ProcessResult GitRepository::MarkResolved(const std::vector<std::string>& paths) const {
    std::vector<std::string> args{"add", "--"}; args.insert(args.end(), paths.begin(), paths.end()); return Run(args);
}
// ---------------------------------------------------------------- branches
ProcessResult GitRepository::Checkout(const std::string& ref) const {
    if (!ValidRefName(ref) && !ValidHash(ref)) return Failure("Invalid branch or commit.");
    return Run({"checkout", ref, "--"});
}
ProcessResult GitRepository::CheckoutRemoteBranch(const Branch& remote, const std::string& localName) const {
    if (!ValidRefName(localName)) return Failure("Invalid branch name.");
    return Run({"checkout", "-b", localName, "--track", remote.name, "--"});
}
ProcessResult GitRepository::CreateBranch(const std::string& name, const std::string& start, bool checkout) const {
    if (!ValidRefName(name)) return Failure("Invalid branch name.");
    if (checkout) return Run({"checkout", "-b", name, start.empty() ? "HEAD" : start, "--"});
    return Run({"branch", name, start.empty() ? "HEAD" : start});
}
ProcessResult GitRepository::DeleteBranch(const std::string& name, bool force) const {
    if (!ValidRefName(name)) return Failure("Invalid branch name.");
    return Run({"branch", force ? "-D" : "-d", name});
}
ProcessResult GitRepository::DeleteRemoteBranch(const Branch& remote) const {
    if (!remote.remote) return Failure("Not a remote branch.");
    return RunNetwork({"push", remote.remoteName(), "--delete", remote.shortName()});
}
ProcessResult GitRepository::RenameBranch(const std::string& from, const std::string& to) const {
    if (!ValidRefName(from) || !ValidRefName(to)) return Failure("Invalid branch name.");
    return Run({"branch", "-m", from, to});
}
ProcessResult GitRepository::SetUpstream(const std::string& branch, const std::string& upstream) const {
    if (upstream.empty()) return Run({"branch", "--unset-upstream", branch});
    return Run({"branch", "--set-upstream-to=" + upstream, branch});
}
ProcessResult GitRepository::CreateTag(const std::string& name, const std::string& target, const std::string& message) const {
    if (!ValidRefName(name)) return Failure("Invalid tag name.");
    if (message.empty()) return Run({"tag", name, target.empty() ? "HEAD" : target});
    return Run({"tag", "-a", "-F", "-", name, target.empty() ? "HEAD" : target}, nullptr, message);
}
ProcessResult GitRepository::DeleteTag(const std::string& name) const {
    if (!ValidRefName(name)) return Failure("Invalid tag name.");
    return Run({"tag", "-d", name});
}
ProcessResult GitRepository::PushTag(const std::string& remote, const std::string& name) const {
    return RunNetwork({"push", remote, "refs/tags/" + name});
}
// ---------------------------------------------------------------- integration
ProcessResult GitRepository::Merge(const std::string& ref, bool noFastForward, bool squash) const {
    if (!ValidRefName(ref) && !ValidHash(ref)) return Failure("Invalid merge source.");
    std::vector<std::string> args{"merge", "--no-edit"};
    if (squash) args.push_back("--squash");
    else if (noFastForward) args.push_back("--no-ff");
    args.push_back(ref);
    return Run(args);
}
ProcessResult GitRepository::Rebase(const std::string& onto, Cancel cancel) const {
    if (!ValidRefName(onto) && !ValidHash(onto)) return Failure("Invalid rebase target.");
    return Run({"rebase", onto}, cancel);
}
ProcessResult GitRepository::CherryPick(const std::vector<std::string>& hashes) const {
    if (hashes.empty()) return Failure("Select a commit to cherry-pick.");
    std::vector<std::string> args{"cherry-pick", "--allow-empty"};
    for (const auto& h : hashes) { if (!ValidHash(h)) return Failure("Invalid commit identifier."); args.push_back(h); }
    return Run(args);
}
ProcessResult GitRepository::Revert(const std::string& hash) const {
    if (!ValidHash(hash)) return Failure("Invalid commit identifier.");
    auto commit = Show(hash);
    if (!commit.ok()) return Failure(commit.error);
    std::vector<std::string> args{"revert", "--no-edit"};
    if (commit.value.merge()) { args.push_back("-m"); args.push_back("1"); }
    args.push_back(hash);
    return Run(args);
}
ProcessResult GitRepository::Reset(const std::string& ref, const std::string& mode) const {
    if (!ValidHash(ref) && !ValidRefName(ref)) return Failure("Invalid reset target.");
    if (mode != "soft" && mode != "mixed" && mode != "hard") return Failure("Invalid reset mode.");
    return Run({"reset", "--" + mode, ref, "--"});
}
ProcessResult GitRepository::Abort() const {
    auto state = State();
    if (!state.ok()) return Failure(state.error);
    if (state.value.rebasing) return Run({"rebase", "--abort"});
    if (state.value.merging) return Run({"merge", "--abort"});
    if (state.value.cherryPicking) return Run({"cherry-pick", "--abort"});
    if (state.value.reverting) return Run({"revert", "--abort"});
    return Failure("No operation is in progress.");
}
ProcessResult GitRepository::Continue() const {
    auto state = State();
    if (!state.ok()) return Failure(state.error);
    if (state.value.rebasing) return Run({"rebase", "--continue"});
    if (state.value.merging) return Run({"commit", "--no-edit"});
    if (state.value.cherryPicking) return Run({"cherry-pick", "--continue"});
    if (state.value.reverting) return Run({"revert", "--continue"});
    return Failure("No operation is in progress.");
}
// ---------------------------------------------------------------- remotes
ProcessResult GitRepository::Fetch(const std::string& remote, bool prune, Cancel cancel) const {
    std::vector<std::string> args{"fetch", "--tags"};
    if (prune) args.push_back("--prune");
    args.push_back(remote);
    return RunNetwork(args, cancel);
}
ProcessResult GitRepository::FetchAll(bool prune, Cancel cancel) const {
    std::vector<std::string> args{"fetch", "--all", "--tags"};
    if (prune) args.push_back("--prune");
    return RunNetwork(args, cancel);
}
ProcessResult GitRepository::Pull(const std::string& remote, const std::string& branch, bool rebase, Cancel cancel) const {
    std::vector<std::string> args{"pull", rebase ? "--rebase" : "--no-rebase"};
    if (!remote.empty()) { args.push_back(remote); if (!branch.empty()) args.push_back(branch); }
    return RunNetwork(args, cancel);
}
ProcessResult GitRepository::Push(const std::string& remote, const std::string& branch, bool setUpstream, bool force, Cancel cancel) const {
    std::vector<std::string> args{"push"};
    if (setUpstream) args.push_back("--set-upstream");
    if (force) args.push_back("--force-with-lease");
    if (!remote.empty()) { args.push_back(remote); if (!branch.empty()) args.push_back(branch); }
    return RunNetwork(args, cancel);
}
ProcessResult GitRepository::FetchPullRequest(const std::string& remote, int number, const std::string& localBranch, Cancel cancel) const {
    if (!ValidRefName(localBranch) || number <= 0) return Failure("Invalid pull request.");
    auto fetch = RunNetwork({"fetch", remote, "+refs/pull/" + std::to_string(number) + "/head:refs/heads/" + localBranch}, cancel);
    if (!fetch.ok()) return fetch;
    return Checkout(localBranch);
}
ProcessResult GitRepository::AddRemote(const std::string& name, const std::string& url) const {
    if (!ValidRefName(name) || url.empty() || url.front() == '-') return Failure("Invalid remote.");
    return Run({"remote", "add", name, url});
}
ProcessResult GitRepository::RemoveRemote(const std::string& name) const { return Run({"remote", "remove", name}); }
ProcessResult GitRepository::SubmoduleUpdate(Cancel cancel) const {
    return RunNetwork({"submodule", "update", "--init", "--recursive"}, cancel);
}
// ---------------------------------------------------------------- stashes
ProcessResult GitRepository::StashSave(const std::string& message, bool includeUntracked, bool keepIndex) const {
    std::vector<std::string> args{"stash", "push"};
    if (includeUntracked) args.push_back("--include-untracked");
    if (keepIndex) args.push_back("--keep-index");
    if (!message.empty()) { args.push_back("-m"); args.push_back(message); }
    return Run(args);
}
ProcessResult GitRepository::StashApply(const Stash& stash, bool pop) const {
    return Run({"stash", pop ? "pop" : "apply", "--index", stash.ref});
}
ProcessResult GitRepository::StashDrop(const Stash& stash) const { return Run({"stash", "drop", stash.ref}); }
// ---------------------------------------------------------------- whole repositories
ProcessResult GitRepository::Clone(const std::string& url, const std::string& destination, const GitOptions& options, Cancel cancel) {
    if (url.empty() || url.front() == '-' || destination.empty()) return Failure("Enter a repository URL and destination.");
    GitRepository parent("/boot/home", options);
    return parent.Run({"clone", "--recurse-submodules", "--", url, destination}, cancel, {}, 16 * 1024 * 1024, options.networkTimeout * 4);
}
ProcessResult GitRepository::Init(const std::string& directory, const GitOptions& options) {
    if (directory.empty()) return Failure("Choose a folder.");
    GitRepository repo(directory, options);
    return repo.Run({"init", "-b", "main"});
}
}
