// Portable core tests: parsers, graph layout, diff patches, settings and a
// live Git repository exercised through the real git executable.
#include "core/DiffParse.h"
#include "core/Git.h"
#include "core/GitExecutable.h"
#include "core/GitHub.h"
#include "core/Graph.h"
#include "core/Settings.h"
#include "core/Undo.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <algorithm>
#include <string>
#include <unistd.h>

using namespace tc;
static int failures = 0, checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)
#define CHECK_EQ(a, b) do { ++checks; auto va = (a); auto vb = (b); if (!(va == vb)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b); } } while (0)

static void TestStatusParsing() {
    std::string data = std::string("M  a.txt\0 M b.txt\0?? c.txt\0UU d.txt\0R  new.txt\0old.txt\0AA e.txt\0", 61);
    auto files = ParseStatus(data);
    CHECK_EQ(files.size(), 6u);
    CHECK(files[0].staged() && !files[0].unstaged());
    CHECK(!files[1].staged() && files[1].unstaged());
    CHECK(files[2].untracked() && files[2].unstaged() && !files[2].staged());
    CHECK(files[3].conflicted() && !files[3].staged());
    CHECK_EQ(files[4].originalPath, std::string("old.txt"));
    CHECK_EQ(files[4].description(true), std::string("renamed"));
    CHECK(files[5].conflicted());
    CHECK_EQ(files[5].description(false), std::string("added by both"));
}
static void TestLogParsing() {
    std::string record = "0123456789abcdef0123456789abcdef01234567\x1f" "fedcba9876543210fedcba9876543210fedcba98 1111111111111111111111111111111111111111\x1f"
        "Ann\x1f" "ann@example.com\x1f" "2026-01-02T03:04:05+00:00\x1f" "2026-01-02T03:04:06+00:00\x1f" "Merge feature\x1f"
        "HEAD -> main, origin/main, tag: v1.0\x1f" "Body line 1\nBody line 2\n";
    std::string data = record + std::string(1, '\0');
    auto commits = ParseLog(data);
    CHECK_EQ(commits.size(), 1u);
    CHECK(commits[0].merge());
    CHECK_EQ(commits[0].parents.size(), 2u);
    CHECK_EQ(commits[0].refs.size(), 3u);
    CHECK_EQ(commits[0].refs[0], std::string("HEAD -> main"));
    CHECK_EQ(commits[0].refs[2], std::string("tag: v1.0"));
    CHECK_EQ(commits[0].body, std::string("Body line 1\nBody line 2"));
    CHECK_EQ(commits[0].shortHash(), std::string("01234567"));
}
static void TestBranchParsing() {
    std::string data = "refs/heads/main\x1f" "aaaa\x1f" "origin/main\x1f" "[ahead 2, behind 1]\x1f*\x1fSubject\x1f" "2026-01-01T00:00:00+00:00\n"
        "refs/heads/feature\x1f" "bbbb\x1f" "origin/feature\x1f" "[gone]\x1f \x1fWork\x1f" "2026-01-01T00:00:00+00:00\n"
        "refs/remotes/origin/HEAD\x1f" "aaaa\x1f\x1f\x1f \x1fSubject\x1f" "2026\n"
        "refs/remotes/origin/main\x1f" "aaaa\x1f\x1f\x1f \x1fSubject\x1f" "2026\n";
    auto branches = ParseBranches(data);
    CHECK_EQ(branches.size(), 3u);
    CHECK(branches[0].current); CHECK_EQ(branches[0].ahead, 2); CHECK_EQ(branches[0].behind, 1);
    CHECK(branches[1].gone);
    CHECK(branches[2].remote); CHECK_EQ(branches[2].remoteName(), std::string("origin")); CHECK_EQ(branches[2].shortName(), std::string("main"));
}
static void TestOtherParsers() {
    auto tags = ParseTags("refs/tags/v1\x1f" "tag\x1f" "1111\x1f" "2222\x1f" "Release\x1f" "2026\nrefs/tags/v0\x1f" "commit\x1f" "3333\x1f" "\x1f" "First\x1f" "2025\n");
    CHECK_EQ(tags.size(), 2u); CHECK(tags[0].annotated); CHECK_EQ(tags[0].hash, std::string("2222")); CHECK_EQ(tags[1].hash, std::string("3333"));
    auto stashes = ParseStashes("stash@{0}\x1f" "abcd\x1fWIP on main: 1234 msg\x1f" "2026-01-01\nstash@{1}\x1f" "ef01\x1fOn feature: saved\x1f" "2025\n");
    CHECK_EQ(stashes.size(), 2u); CHECK_EQ(stashes[1].index, 1); CHECK_EQ(stashes[1].message, std::string("On feature: saved"));
    auto remotes = ParseRemotes("origin\thttps://github.com/o/r.git (fetch)\norigin\thttps://github.com/o/r.git (push)\nfork\tgit@github.com:me/r.git (fetch)\n");
    CHECK_EQ(remotes.size(), 2u); CHECK_EQ(remotes[0].pushURL, std::string("https://github.com/o/r.git"));
    CHECK_EQ(GitHubRepository("git@github.com:me/r.git").value_or(""), std::string("me/r"));
    CHECK_EQ(GitHubRepository("https://github.com/o/r").value_or(""), std::string("o/r"));
    CHECK(!GitHubRepository("https://gitlab.com/o/r").has_value());
    CHECK(!GitHubRepository("https://github.com/o/r/extra").has_value());
    auto modules = ParseSubmodules(" 0123456789abcdef0123456789abcdef01234567 vendor/lib (v1.2-3-gabc)\n+aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa other\n");
    CHECK_EQ(modules.size(), 2u); CHECK_EQ(modules[0].path, std::string("vendor/lib")); CHECK_EQ(modules[0].describe, std::string("v1.2-3-gabc")); CHECK_EQ(modules[1].state, '+');
    std::string names("M\0a.txt\0R100\0old.txt\0new.txt\0A\0bin.png\0", 39);
    auto files = ParseNameStatus(names);
    CHECK_EQ(files.size(), 3u); CHECK_EQ(files[1].originalPath, std::string("old.txt")); CHECK_EQ(files[1].path, std::string("new.txt"));
    std::string numstat("3\t1\ta.txt\0-\t-\tbin.png\0", 22);
    MergeNumstat(files, numstat);
    CHECK_EQ(files[0].added, 3); CHECK_EQ(files[0].deleted, 1); CHECK(files[2].binary);
    CHECK(ValidRefName("feature/x-1")); CHECK(!ValidRefName("-bad")); CHECK(!ValidRefName("a..b")); CHECK(!ValidRefName("x.lock")); CHECK(!ValidRefName("a b"));
    CHECK_EQ(Base64("x-access-token:abc"), std::string("eC1hY2Nlc3MtdG9rZW46YWJj"));
    CHECK_EQ(ParseGitVersion("git version 2.54.0.windows.1\n"), std::string("2.54.0"));
    CHECK(CompareGitVersions("2.54.0", "2.45.2") > 0); CHECK(CompareGitVersions("2.9", "2.10") < 0); CHECK_EQ(CompareGitVersions("2.1.0", "2.1"), 0);
    auto directories = GitSearchDirectories("/opt/git/bin:/boot/system/bin");
    CHECK(directories.front() == "/boot/system/bin"); CHECK(directories.back() == "/opt/git/bin");
}
// Checks the drawing contract: pass-throughs keep their column, links only
// start or end at the node, and the columns (with colours) leaving a row at
// the bottom are exactly the columns entering the next row at the top.
static bool GraphContinuous(const std::vector<GraphRow>& rows, std::string* why = nullptr) {
    auto fail = [&](const std::string& reason) { if (why) *why = reason; return false; };
    for (size_t r = 0; r < rows.size(); ++r) {
        const GraphRow& row = rows[r];
        for (const auto& e : row.up) if (e.from != e.to && e.to != row.lane) return fail("up link not into node at row " + std::to_string(r));
        for (const auto& e : row.down) if (e.from != e.to && e.from != row.lane) return fail("down link not from node at row " + std::to_string(r));
        for (const auto& e : row.up) if (e.from == e.to && e.from == row.lane) {
            bool intoNode = true; (void)intoNode;   // a straight line into the node's column is the node's own lane
        }
        if (r + 1 == rows.size()) break;
        std::map<int, int> bottom, top;
        for (const auto& e : row.down) { auto it = bottom.find(e.to); if (it != bottom.end() && it->second != e.color) return fail("two colours leave column at row " + std::to_string(r)); bottom[e.to] = e.color; }
        for (const auto& e : rows[r + 1].up) { if (top.count(e.from)) return fail("column enters twice at row " + std::to_string(r + 1)); top[e.from] = e.color; }
        if (bottom != top) return fail("discontinuity between rows " + std::to_string(r) + " and " + std::to_string(r + 1));
    }
    return true;
}
static void TestGraph() {
    // main: C (merge of B and F) -> B -> A ; feature: F -> A
    Commit c; c.hash = "c"; c.parents = {"b", "f"};
    Commit f; f.hash = "f"; f.parents = {"a"};
    Commit b; b.hash = "b"; b.parents = {"a"};
    Commit a; a.hash = "a";
    auto rows = LayoutGraph({c, f, b, a});
    CHECK_EQ(rows.size(), 4u);
    CHECK(GraphContinuous(rows));
    CHECK_EQ(rows[0].lane, 0); CHECK(rows[0].merge);
    CHECK_EQ(rows[1].lane, 1);                          // the merged branch sits in the next column
    CHECK_EQ(rows[2].lane, 0);                          // main keeps its column
    CHECK_EQ(rows[3].lane, 0);
    bool converges = false;                             // the branch column joins the root's column
    for (const auto& e : rows[3].up) if (e.from == 1 && e.to == 0) converges = true;
    CHECK(converges);
    // Columns never slide: when an inner branch ends, the outer one stays put.
    Commit m2; m2.hash = "m2"; m2.parents = {"m1", "y"};
    Commit m1; m1.hash = "m1"; m1.parents = {"base", "x"};
    Commit y; y.hash = "y"; y.parents = {"base"};
    Commit x; x.hash = "x"; x.parents = {"base"};
    Commit base; base.hash = "base";
    auto nested = LayoutGraph({m2, m1, y, x, base});
    CHECK(GraphContinuous(nested));
    CHECK_EQ(nested[2].lane, 1); CHECK_EQ(nested[3].lane, 2);
    for (const auto& row : nested) for (const auto& e : row.down) if (e.from != row.lane) CHECK_EQ(e.from, e.to);
    // A freed column is reused by the next branch tip instead of widening.
    Commit t1; t1.hash = "t1"; t1.parents = {"p"}; Commit t2; t2.hash = "t2"; t2.parents = {"p"}; Commit p; p.hash = "p";
    auto twin = LayoutGraph({t1, t2, p});
    CHECK(GraphContinuous(twin));
    CHECK_EQ(twin[1].lane, 1); CHECK_EQ(twin[2].lane, 0); CHECK_EQ(twin[2].lanes, 1);
    Commit r1; r1.hash = "r1"; Commit r2; r2.hash = "r2";
    auto roots = LayoutGraph({r1, r2});
    CHECK_EQ(roots[0].lane, 0); CHECK_EQ(roots[1].lane, 0);
    // Random DAGs in topological order, including octopus merges and parents
    // outside the loaded page.
    unsigned seed = 12345;
    auto rnd = [&](unsigned n) { seed = seed * 1103515245u + 12345u; return (seed >> 8) % n; };
    for (int trial = 0; trial < 300; ++trial) {
        int count = 5 + static_cast<int>(rnd(120));
        std::vector<Commit> commits(count);
        for (int i = 0; i < count; ++i) commits[i].hash = "h" + std::to_string(trial) + "_" + std::to_string(i);
        for (int i = 0; i < count; ++i) {
            int parents = rnd(10) < 7 ? 1 : rnd(10) < 8 ? 2 : rnd(4) == 0 ? 0 : 3;
            for (int k = 0; k < parents; ++k) {
                int later = i + 1 + static_cast<int>(rnd(12));
                std::string parent = later < count ? commits[later].hash : "outside" + std::to_string(rnd(3));
                if (std::find(commits[i].parents.begin(), commits[i].parents.end(), parent) == commits[i].parents.end()) commits[i].parents.push_back(parent);
            }
        }
        std::string why;
        auto layout = LayoutGraph(commits);
        bool ok = GraphContinuous(layout, &why);
        if (!ok) std::fprintf(stderr, "trial %d: %s\n", trial, why.c_str());
        CHECK(ok);
    }
}
static const char* kDiff =
    "diff --git a/file.txt b/file.txt\n"
    "index 1111111..2222222 100644\n"
    "--- a/file.txt\n"
    "+++ b/file.txt\n"
    "@@ -1,4 +1,5 @@\n"
    " one\n"
    "-two\n"
    "+two changed\n"
    "+two added\n"
    " three\n"
    " four\n"
    "@@ -10,3 +11,2 @@ context here\n"
    " ten\n"
    "-eleven\n"
    " twelve\n"
    "diff --git a/image.png b/image.png\n"
    "Binary files a/image.png and b/image.png differ\n";
static void TestDiffParsing() {
    auto files = ParseUnifiedDiff(kDiff);
    CHECK_EQ(files.size(), 2u);
    CHECK_EQ(files[0].hunks.size(), 2u);
    CHECK_EQ(files[0].displayPath(), std::string("file.txt"));
    CHECK(files[1].binary);
    const auto& h = files[0].hunks[0];
    CHECK_EQ(h.added(), 2); CHECK_EQ(h.removed(), 1);
    CHECK_EQ(h.lines[1].oldNumber, 2L); CHECK_EQ(h.lines[2].newNumber, 2L); CHECK_EQ(h.lines[4].newNumber, 4L);
    CHECK_EQ(files[0].hunks[1].header, std::string("@@ -10,3 +11,2 @@ context here"));
    auto whole = HunkPatch(files[0], h);
    CHECK(whole.find("--- a/file.txt\n+++ b/file.txt\n@@ -1,4 +1,5 @@\n one\n-two\n+two changed\n+two added\n three\n four\n") != std::string::npos);
    auto partial = HunkPatch(files[0], h, {2});   // only "+two changed"
    CHECK(partial.find("@@ -1,4 +1,5 @@\n one\n two\n+two changed\n three\n four\n") != std::string::npos);
    auto removeOnly = HunkPatch(files[0], h, {1});
    CHECK(removeOnly.find("@@ -1,4 +1,3 @@\n one\n-two\n three\n four\n") != std::string::npos);
    auto second = HunkPatch(files[0], files[0].hunks[1]);
    CHECK(second.find("@@ -10,3 +11,2 @@ context here\n ten\n-eleven\n twelve\n") != std::string::npos);
    auto file = FilePatch(files[0]);
    CHECK(file.find("@@ -1,4 +1,5 @@") != std::string::npos && file.find("@@ -10,3 +11,2 @@") != std::string::npos);
}
static void TestSettings() {
    char path[] = "/tmp/turbochook-settings-XXXXXX";
    int fd = mkstemp(path); close(fd);
    Settings settings(path);
    settings.Set("gitExecutable", "/boot/system/bin/git");
    settings.SetBool("prune", true);
    settings.SetInt("context", 5);
    settings.TouchRepository("/a"); settings.TouchRepository("/b"); settings.TouchRepository("/a");
    CHECK(settings.Save());
    Settings again(path);
    CHECK(again.Load());
    CHECK_EQ(again.Get("gitExecutable"), std::string("/boot/system/bin/git"));
    CHECK(again.GetBool("prune")); CHECK_EQ(again.GetInt("context"), 5);
    auto repos = again.Repositories();
    CHECK_EQ(repos.size(), 2u); CHECK_EQ(repos[0], std::string("/a")); CHECK_EQ(repos[1], std::string("/b"));
    again.ForgetRepository("/a");
    CHECK_EQ(again.Repositories().size(), 1u);
    unlink(path);
}
static void TestGitHubParsing() {
    auto prs = ParsePullRequests(R"([{"number": 7, "title": "Add x", "state": "open", "draft": true, "html_url": "https://github.com/o/r/pull/7",
        "user": {"login": "ann"}, "head": {"ref": "feature", "sha": "abc", "repo": {"full_name": "ann/r", "clone_url": "https://github.com/ann/r.git"}},
        "base": {"ref": "main"}, "merged_at": null, "comments": 3}])");
    CHECK_EQ(prs.size(), 1u);
    CHECK_EQ(prs[0].number, 7); CHECK(prs[0].draft); CHECK_EQ(prs[0].author, std::string("ann"));
    CHECK_EQ(prs[0].headRef, std::string("feature")); CHECK_EQ(prs[0].headRepository, std::string("ann/r")); CHECK_EQ(prs[0].baseRef, std::string("main"));
    CHECK_EQ(prs[0].statusLine(), std::string("draft"));
    auto merged = ParsePullRequest(R"({"number": 8, "state": "closed", "merged_at": "2026-01-01T00:00:00Z", "mergeable": false})");
    CHECK(merged.merged); CHECK_EQ(merged.statusLine(), std::string("merged")); CHECK_EQ(merged.mergeable, std::string("conflicting"));
    HttpResponse response; response.status = 422; response.body = R"({"message": "Validation Failed", "errors": [{"message": "No commits between main and feature"}]})";
    auto message = GitHubErrorMessage(response);
    CHECK(message.find("Validation Failed") != std::string::npos && message.find("No commits") != std::string::npos);
}
static std::string ReadAll(const std::string& path) { std::ifstream in(path); return std::string((std::istreambuf_iterator<char>(in)), {}); }
static void WriteAll(const std::string& path, const std::string& text) { std::ofstream out(path); out << text; }
static void TestLiveRepository() {
    auto installations = DiscoverGitInstallations();
    if (installations.empty()) { std::fprintf(stderr, "skip: git not installed\n"); return; }
    CHECK(installations.front().usable());
    char root[] = "/tmp/turbochook-repo-XXXXXX";
    if (!mkdtemp(root)) { ++failures; return; }
    std::string dir = root;
    GitOptions options; options.executable = installations.front().path;
    CHECK(GitRepository::Init(dir, options).ok());
    GitRepository repo(dir, options);
    CHECK(repo.IsRepository());
    CHECK(repo.SetConfig("user.name", "Test").ok()); CHECK(repo.SetConfig("user.email", "t@example.com").ok());
    WriteAll(dir + "/file.txt", "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\neleven\ntwelve\n");
    CHECK(repo.Stage({"file.txt"}).ok());
    CHECK(repo.CommitIndex("Initial", false).ok());
    auto state = repo.State();
    CHECK(state.ok() && state.value.head == "main" && !state.value.inProgress());
    // Two hunks; stage only the first through the diff parser.
    WriteAll(dir + "/file.txt", "one\ntwo changed\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\neleven changed\ntwelve\n");
    auto diff = repo.WorkingDiff("file.txt", false);
    CHECK(diff.ok());
    auto files = ParseUnifiedDiff(diff.output);
    CHECK_EQ(files.size(), 1u); CHECK_EQ(files[0].hunks.size(), 2u);
    CHECK(repo.ApplyPatch(HunkPatch(files[0], files[0].hunks[0]), true, false).ok());
    auto status = repo.Status();
    CHECK(status.ok() && status.value.size() == 1 && status.value[0].index == 'M' && status.value[0].worktree == 'M');
    auto staged = ParseUnifiedDiff(repo.WorkingDiff("file.txt", true).output);
    CHECK(staged.size() == 1 && staged[0].hunks.size() == 1 && staged[0].hunks[0].lines[1].text == "two");
    // Unstage the hunk again, then discard the second hunk from the worktree.
    CHECK(repo.ApplyPatch(HunkPatch(staged[0], staged[0].hunks[0]), true, true).ok());
    auto unstagedDiff = ParseUnifiedDiff(repo.WorkingDiff("file.txt", false).output);
    CHECK(unstagedDiff.size() == 1 && unstagedDiff[0].hunks.size() == 2);
    CHECK(repo.ApplyPatch(HunkPatch(unstagedDiff[0], unstagedDiff[0].hunks[1]), false, true).ok());
    CHECK(ReadAll(dir + "/file.txt").find("eleven changed") == std::string::npos);
    CHECK(ReadAll(dir + "/file.txt").find("two changed") != std::string::npos);
    // Backups capture and restore the working copy (Tower-style undo of discard).
    auto backup = repo.CreateBackup();
    CHECK(!backup.empty());
    CHECK(repo.Discard(repo.Status().value).ok());
    CHECK(ReadAll(dir + "/file.txt").find("two changed") == std::string::npos);
    CHECK(repo.RestoreBackup(backup).ok());
    CHECK(ReadAll(dir + "/file.txt").find("two changed") != std::string::npos);
    CHECK(repo.StageAll().ok()); CHECK(repo.CommitIndex("Second", false).ok());
    // Branch, commit, merge, log with graph.
    CHECK(repo.CreateBranch("feature", "", true).ok());
    WriteAll(dir + "/feature.txt", "feature\n");
    CHECK(repo.Stage({"feature.txt"}).ok()); CHECK(repo.CommitIndex("Feature work", false).ok());
    CHECK(repo.Checkout("main").ok());
    WriteAll(dir + "/main.txt", "main\n");
    CHECK(repo.Stage({"main.txt"}).ok()); CHECK(repo.CommitIndex("Main work", false).ok());
    CHECK(repo.Merge("feature", true).ok());
    auto log = repo.Log({}, 0, 50);
    CHECK(log.ok() && log.value.size() == 5 && log.value[0].merge());
    auto rows = LayoutGraph(log.value);
    CHECK_EQ(rows.size(), 5u);
    CHECK(GraphContinuous(rows));
    CHECK(rows[0].merge && rows[0].down.size() >= 2);
    auto branches = repo.Branches();
    CHECK(branches.ok() && branches.value.size() == 2);
    auto commitFiles = repo.CommitFiles(log.value[0].hash);
    CHECK(commitFiles.ok() && commitFiles.value.size() == 1 && commitFiles.value[0].path == "feature.txt" && commitFiles.value[0].added == 1);
    auto commitDiff = repo.CommitDiff(log.value[0].hash, "feature.txt");
    CHECK(commitDiff.ok() && commitDiff.output.find("+feature") != std::string::npos);
    // Tags, stashes, revert, reset, cherry-pick.
    CHECK(repo.CreateTag("v1", "", "Release one").ok());
    auto tags = repo.Tags();
    CHECK(tags.ok() && tags.value.size() == 1 && tags.value[0].annotated && tags.value[0].hash == log.value[0].hash);
    WriteAll(dir + "/main.txt", "main changed\n");
    CHECK(repo.StashSave("wip", false, false).ok());
    auto stashes = repo.Stashes();
    CHECK(stashes.ok() && stashes.value.size() == 1 && stashes.value[0].message.find("wip") != std::string::npos);
    auto stashFiles = repo.StashFiles(stashes.value[0]);
    CHECK(stashFiles.ok() && stashFiles.value.size() == 1);
    CHECK(repo.StashApply(stashes.value[0], true).ok());
    CHECK(ReadAll(dir + "/main.txt") == "main changed\n");
    CHECK(repo.Discard(repo.Status().value).ok());
    CHECK(repo.Revert(log.value[1].hash).ok() || repo.Revert(log.value[2].hash).ok());
    auto afterRevert = repo.Log({"main"}, 0, 10);
    CHECK(afterRevert.ok() && afterRevert.value.size() == 6);
    CHECK(repo.Reset(log.value[0].hash, "hard").ok());
    CHECK(repo.Log({"main"}, 0, 10).value.size() == 5);
    auto featureLog = repo.Log({"feature"}, 0, 10);
    CHECK(repo.Checkout("feature").ok());
    CHECK(repo.CherryPick({log.value[1].hash == featureLog.value[0].hash ? log.value[2].hash : log.value[1].hash}).ok());
    CHECK(repo.Log({"feature"}, 0, 10).value.size() == 4);
    CHECK(repo.Checkout("main").ok());
    CHECK(repo.DeleteBranch("feature", true).ok());
    CHECK(repo.Branches().value.size() == 1);
    auto reflog = repo.Reflog(5);
    CHECK(reflog.ok() && !reflog.value.empty());
    // Conflict state detection and abort.
    CHECK(repo.CreateBranch("other", log.value[1].hash == featureLog.value[0].hash ? log.value[2].hash : log.value[1].hash, true).ok());
    WriteAll(dir + "/main.txt", "other side\n");
    CHECK(repo.Stage({"main.txt"}).ok()); CHECK(repo.CommitIndex("Other", false).ok());
    CHECK(repo.Checkout("main").ok());
    WriteAll(dir + "/main.txt", "main side\n");
    CHECK(repo.Stage({"main.txt"}).ok()); CHECK(repo.CommitIndex("Main side", false).ok());
    CHECK(!repo.Merge("other", false).ok());
    auto merging = repo.State();
    CHECK(merging.ok() && merging.value.merging);
    auto conflicts = repo.Status();
    CHECK(conflicts.ok() && conflicts.value.size() == 1 && conflicts.value[0].conflicted());
    CHECK(repo.ResolveWith("main.txt", false).ok());
    CHECK(ReadAll(dir + "/main.txt") == "other side\n");
    CHECK(repo.Abort().ok());
    CHECK(!repo.State().value.merging);
    // Undo stack semantics.
    UndoStack undo;
    undo.Push("Undo something", [&] { return repo.Run({"status"}); });
    CHECK(!undo.Empty()); CHECK_EQ(undo.Description(), std::string("Undo something"));
    CHECK(undo.Pop().ok()); CHECK(undo.Empty());
    std::string cleanup = "rm -rf " + dir;
    (void)system(cleanup.c_str());
}
int main() {
    TestStatusParsing(); TestLogParsing(); TestBranchParsing(); TestOtherParsers(); TestGraph();
    TestDiffParsing(); TestSettings(); TestGitHubParsing(); TestLiveRepository();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
