// Command handlers for RepositoryWindow: every user-facing Git operation,
// its confirmation, and how it is undone.
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
#include <Clipboard.h>
#include <algorithm>
#include <fstream>

namespace tc {
namespace {
std::string Text(BMessage* message, const char* name) { const char* value = nullptr; return message->FindString(name, &value) == B_OK && value ? value : ""; }
bool Flag(BMessage* message, const char* name) { bool value = false; message->FindBool(name, &value); return value; }
std::string Join(const std::vector<std::string>& items, const char* separator = ", ") {
    std::string out;
    for (size_t i = 0; i < items.size(); ++i) out += (i ? separator : "") + items[i];
    return out;
}
std::string Short(const std::string& hash) { return hash.substr(0, 8); }
}
// ---------------------------------------------------------------- working copy
void RepositoryWindow::HandleWorkingCopy(BMessage* message) {
    auto repo = fRepo;
    auto paths = Paths(message);
    switch (message->what) {
        case kStage: {
            if (paths.empty()) break;
            RunOperation("Stage " + Join(paths), [repo, paths](const std::atomic<bool>&) { return repo->Stage(paths); },
                [this, repo, paths](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo stage " + Join(paths), [repo, paths] { return repo->Unstage(paths); }); });
            break;
        }
        case kUnstage: {
            if (paths.empty()) break;
            RunOperation("Unstage " + Join(paths), [repo, paths](const std::atomic<bool>&) { return repo->Unstage(paths); },
                [this, repo, paths](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo unstage " + Join(paths), [repo, paths] { return repo->Stage(paths); }); });
            break;
        }
        case kStageAll: {
            std::vector<std::string> unstaged;
            for (const auto& f : fFiles) if (f.unstaged() || f.conflicted()) unstaged.push_back(f.path);
            if (unstaged.empty()) break;
            RunOperation("Stage all changes", [repo](const std::atomic<bool>&) { return repo->StageAll(); },
                [this, repo, unstaged](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo stage all", [repo, unstaged] { return repo->Unstage(unstaged); }); });
            break;
        }
        case kUnstageAll: {
            std::vector<std::string> staged;
            for (const auto& f : fFiles) if (f.staged()) staged.push_back(f.path);
            if (staged.empty()) break;
            RunOperation("Unstage all changes", [repo](const std::atomic<bool>&) { return repo->UnstageAll(); },
                [this, repo, staged](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo unstage all", [repo, staged] { return repo->Stage(staged); }); });
            break;
        }
        case kDiscard: case kDiscardAll: {
            std::vector<FileStatus> files;
            for (const auto& f : fFiles) {
                bool chosen = message->what == kDiscardAll ? (f.unstaged() && !f.conflicted()) : std::find(paths.begin(), paths.end(), f.path) != paths.end();
                if (chosen) files.push_back(f);
            }
            if (files.empty()) break;
            int untracked = static_cast<int>(std::count_if(files.begin(), files.end(), [](const FileStatus& f) { return f.untracked(); }));
            std::string what = files.size() == 1 ? files.front().path : std::to_string(files.size()) + " files";
            std::string warning = "Discard the changes in " + what + "?";
            if (untracked) warning += "\n\n" + std::to_string(untracked) + " untracked file(s) will be deleted permanently; tracked changes can be undone.";
            if (!Confirm("Discard Changes", warning, "Discard")) break;
            RunOperation("Discard changes", [repo, files, this](const std::atomic<bool>&) {
                std::string backup = repo->CreateBackup();
                auto result = repo->Discard(files);
                if (result.ok() && !backup.empty()) result.output = backup;   // carry the backup hash to the callback
                return result;
            }, [this, repo, what](const ProcessResult& r) {
                if (!r.ok()) return;
                std::string backup = Trim(r.output);
                if (ValidHash(backup)) RecordUndo("Undo discard " + what, [repo, backup] { return repo->RestoreBackup(backup); });
            });
            break;
        }
        case kCommit: {
            std::string text = fWorkingCopy->Message();
            bool amend = fWorkingCopy->Amend();
            if (Trim(text).empty()) { ShowError("Commit", "Enter a commit message first."); fWorkingCopy->FocusMessage(); break; }
            bool staged = std::any_of(fFiles.begin(), fFiles.end(), [](const FileStatus& f) { return f.staged(); });
            if (!staged && !amend) { ShowError("Commit", "Stage some changes first."); break; }
            std::string oldHead = fModel.state.headHash;
            RunOperation(amend ? "Amend commit" : "Commit", [repo, text, amend](const std::atomic<bool>&) { return repo->CommitIndex(text, amend, amend); },
                [this, repo, oldHead, amend](const ProcessResult& r) {
                    if (!r.ok()) return;
                    fWorkingCopy->ClearMessage();
                    if (!oldHead.empty()) RecordUndo(amend ? "Undo amend" : "Undo commit", [repo, oldHead] { return repo->Reset(oldHead, "soft"); });
                    else RecordUndo("Undo initial commit", [repo] { return repo->Run({"update-ref", "-d", "HEAD"}); });
                });
            break;
        }
        case kAmendToggled: {
            bool amend = message->HasBool("amend") ? Flag(message, "amend") : !fWorkingCopy->Amend();
            fWorkingCopy->SetAmend(amend, fPreviousCommitMessage);
            break;
        }
        case kStageHunk: case kStageLines: {
            std::string patch = Text(message, "patch"), path = Text(message, "path");
            RunOperation("Stage hunk in " + path, [repo, patch](const std::atomic<bool>&) { return repo->ApplyPatch(patch, true, false); },
                [this, repo, patch, path](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo stage hunk in " + path, [repo, patch] { return repo->ApplyPatch(patch, true, true); }); });
            break;
        }
        case kUnstageHunk: case kUnstageLines: {
            std::string patch = Text(message, "patch"), path = Text(message, "path");
            RunOperation("Unstage hunk in " + path, [repo, patch](const std::atomic<bool>&) { return repo->ApplyPatch(patch, true, true); },
                [this, repo, patch, path](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo unstage hunk in " + path, [repo, patch] { return repo->ApplyPatch(patch, true, false); }); });
            break;
        }
        case kDiscardHunk: case kDiscardLines: {
            std::string patch = Text(message, "patch"), path = Text(message, "path");
            if (!Confirm("Discard Hunk", "Discard these changes in " + path + "? This can be undone.", "Discard")) break;
            RunOperation("Discard hunk in " + path, [repo, patch](const std::atomic<bool>&) { return repo->ApplyPatch(patch, false, true); },
                [this, repo, patch, path](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo discard hunk in " + path, [repo, patch] { return repo->ApplyPatch(patch, false, false); }); });
            break;
        }
        case kResolveOurs: case kResolveTheirs: {
            bool ours = message->what == kResolveOurs;
            if (paths.empty()) break;
            RunOperation(std::string("Resolve with ") + (ours ? "ours" : "theirs"), [repo, paths, ours](const std::atomic<bool>&) {
                ProcessResult last;
                for (const auto& p : paths) { last = repo->ResolveWith(p, ours); if (!last.ok()) return last; }
                return last;
            });
            break;
        }
        case kMarkResolved: {
            if (paths.empty()) break;
            RunOperation("Mark resolved", [repo, paths](const std::atomic<bool>&) { return repo->MarkResolved(paths); });
            break;
        }
        case kIgnoreFile: {
            if (paths.empty()) break;
            std::ofstream out(fRoot + "/.gitignore", std::ios::app);
            for (const auto& p : paths) out << p << '\n';
            Refresh();
            break;
        }
        case kContinueOperation: {
            if (!fModel.state.inProgress()) { ShowError("Continue", "No merge, rebase, cherry-pick or revert is in progress."); break; }
            RunOperation("Continue " + fModel.state.description(), [repo](const std::atomic<bool>&) { return repo->Continue(); });
            break;
        }
        case kAbortOperation: {
            if (!fModel.state.inProgress()) { ShowError("Abort", "No merge, rebase, cherry-pick or revert is in progress."); break; }
            if (!Confirm("Abort Operation", "Abort the " + fModel.state.description() + " and return to the previous state?", "Abort")) break;
            RunOperation("Abort", [repo](const std::atomic<bool>&) { return repo->Abort(); });
            break;
        }
        default: break;
    }
}
// ---------------------------------------------------------------- branch helpers
void RepositoryWindow::CheckoutBranch(const std::string& name) {
    auto repo = fRepo;
    std::string previous = fModel.state.detached ? fModel.state.headHash : fModel.state.head;
    RunOperation("Checkout " + name, [repo, name](const std::atomic<bool>&) { return repo->Checkout(name); },
        [this, repo, previous, name](const ProcessResult& r) { if (r.ok() && !previous.empty()) RecordUndo("Undo checkout " + name, [repo, previous] { return repo->Checkout(previous); }); });
}
void RepositoryWindow::MergeInto(const std::string& source, const std::string& target) {
    auto repo = fRepo;
    bool switching = !target.empty() && target != fModel.state.head;
    std::string oldHead = fModel.state.headHash, oldBranch = fModel.state.head;
    bool squash = false;
    std::vector<std::string> choices = {"Merge (fast-forward when possible)", "Merge with a merge commit (--no-ff)", "Squash merge (stage the result)"};
    int choice = 0;
    {
        // Ask how to merge; the prompt returns synchronously through a modal alert-style popup.
        BPoint where(Frame().left + Frame().Width() / 2, Frame().top + 120);
        uint32 command = PopupChoice(fSidebar, where, {{choices[0], 1}, {choices[1], 2}, {choices[2], 3}});
        if (!command) return;
        choice = static_cast<int>(command) - 1;
    }
    squash = choice == 2;
    bool noFF = choice == 1;
    RunOperation("Merge " + source + " into " + (target.empty() ? oldBranch : target), [repo, source, target, switching, noFF, squash](const std::atomic<bool>&) {
        if (switching) { auto co = repo->Checkout(target); if (!co.ok()) return co; }
        return repo->Merge(source, noFF, squash);
    }, [this, repo, oldHead, oldBranch, switching, source](const ProcessResult& r) {
        if (!r.ok()) return;
        RecordUndo("Undo merge " + source, [repo, oldHead, oldBranch, switching] {
            auto reset = repo->Reset("HEAD", "hard");
            if (!reset.ok()) return reset;
            if (switching) { auto co = repo->Checkout(oldBranch); if (!co.ok()) return co; return repo->Run({"status"}); }
            return repo->Reset(oldHead, "hard");
        });
    });
}
void RepositoryWindow::RebaseOnto(const std::string& branch, const std::string& onto) {
    auto repo = fRepo;
    bool switching = !branch.empty() && branch != fModel.state.head;
    std::string oldHead = fModel.state.headHash, oldBranch = fModel.state.head;
    if (!Confirm("Rebase", "Rebase " + (branch.empty() ? oldBranch : branch) + " onto " + onto + "?\n\nThis rewrites the branch's commits. It can be undone while this window stays open.", "Rebase", false)) return;
    RunOperation("Rebase onto " + onto, [repo, branch, onto, switching](const std::atomic<bool>& cancel) {
        if (switching) { auto co = repo->Checkout(branch); if (!co.ok()) return co; }
        return repo->Rebase(onto, &cancel);
    }, [this, repo, oldHead, oldBranch, switching, onto](const ProcessResult& r) {
        if (!r.ok()) return;
        RecordUndo("Undo rebase onto " + onto, [repo, oldHead, oldBranch, switching] {
            if (switching) { auto co = repo->Checkout(oldBranch); if (!co.ok()) return co; }
            return repo->Reset(oldHead, "hard");
        });
    });
}
void RepositoryWindow::DeleteBranch(const std::string& name, bool remote) {
    auto repo = fRepo;
    if (remote) {
        Branch branch; bool found = false;
        for (const auto& b : fModel.branches) if (b.remote && b.name == name) { branch = b; found = true; }
        if (!found) return;
        if (!Confirm("Delete Remote Branch", "Delete " + name + " on the remote " + branch.remoteName() + "? This cannot be undone from here.", "Delete")) return;
        RunOperation("Delete " + name, [repo, branch](const std::atomic<bool>&) { return repo->DeleteRemoteBranch(branch); }, {}, true);
        return;
    }
    if (name == fModel.state.head) { ShowError("Delete Branch", "Checkout another branch before deleting " + name + "."); return; }
    Branch branch;
    for (const auto& b : fModel.branches) if (!b.remote && b.name == name) branch = b;
    if (!Confirm("Delete Branch", "Delete the branch " + name + "?\n\nUnmerged commits stay recoverable through Undo while this window is open.", "Delete")) return;
    RunOperation("Delete branch " + name, [repo, name](const std::atomic<bool>&) { return repo->DeleteBranch(name, true); },
        [this, repo, branch](const ProcessResult& r) {
            if (!r.ok()) return;
            RecordUndo("Undo delete branch " + branch.name, [repo, branch] {
                auto create = repo->CreateBranch(branch.name, branch.hash, false);
                if (create.ok() && !branch.upstream.empty()) repo->SetUpstream(branch.name, branch.upstream);
                return create;
            });
        });
}
void RepositoryWindow::CherryPickOnto(const std::vector<std::string>& hashes, const std::string& branch) {
    auto repo = fRepo;
    bool switching = !branch.empty() && branch != fModel.state.head;
    std::string oldHead = fModel.state.headHash, oldBranch = fModel.state.head;
    std::vector<std::string> ordered(hashes.rbegin(), hashes.rend());   // history lists newest first; apply oldest first
    RunOperation("Cherry-pick " + std::to_string(hashes.size()) + (hashes.size() == 1 ? " commit" : " commits"), [repo, ordered, branch, switching](const std::atomic<bool>&) {
        if (switching) { auto co = repo->Checkout(branch); if (!co.ok()) return co; }
        return repo->CherryPick(ordered);
    }, [this, repo, oldHead, oldBranch, switching, branch](const ProcessResult& r) {
        if (!r.ok()) return;
        RecordUndo("Undo cherry-pick", [repo, oldHead, oldBranch, switching, branch] {
            if (switching) {
                // The target branch received the commits: move it back, then return to the previous branch.
                auto target = repo->Run({"rev-parse", "--verify", "-q", branch + "@{1}"});
                if (target.ok()) repo->Run({"branch", "-f", branch, Trim(target.output)});
                return repo->Checkout(oldBranch);
            }
            return repo->Reset(oldHead, "hard");
        });
    });
}
void RepositoryWindow::PushBranch(const std::string& branch, const std::string& remote, bool force) {
    auto repo = fRepo;
    bool setUpstream = true;
    for (const auto& b : fModel.branches) if (!b.remote && b.name == branch && !b.upstream.empty()) setUpstream = false;
    if (force && !Confirm("Force Push", "Force push " + branch + " to " + remote + "? Remote commits not in this branch will be lost (with lease protection).", "Force Push")) return;
    RunOperation((force ? "Force push " : "Push ") + branch + " to " + remote, [repo, branch, remote, setUpstream, force](const std::atomic<bool>& cancel) {
        return repo->Push(remote, branch, setUpstream, force, &cancel);
    }, {}, true);
}
void RepositoryWindow::ApplyStash(const std::string& ref, bool pop) {
    auto repo = fRepo;
    Stash stash; bool found = false;
    for (const auto& s : fModel.stashes) if (s.ref == ref) { stash = s; found = true; }
    if (!found) return;
    RunOperation(std::string(pop ? "Pop" : "Apply") + " stash " + ref, [repo, stash, pop, this](const std::atomic<bool>&) {
        std::string backup = repo->CreateBackup();
        auto result = repo->StashApply(stash, pop);
        if (result.ok()) result.output = backup;
        return result;
    }, [this, repo, stash, pop](const ProcessResult& r) {
        if (!r.ok()) return;
        std::string backup = Trim(r.output);
        RecordUndo(std::string("Undo ") + (pop ? "pop" : "apply") + " stash", [repo, stash, pop, backup] {
            auto reset = repo->Reset("HEAD", "hard");
            if (!reset.ok()) return reset;
            repo->Run({"clean", "-fd", "-q"});
            if (ValidHash(backup)) repo->RestoreBackup(backup);
            if (pop) return repo->Run({"stash", "store", "-m", stash.message, stash.hash});
            return reset;
        });
    });
}
// ---------------------------------------------------------------- branches, tags, stashes
void RepositoryWindow::HandleBranches(BMessage* message) {
    auto repo = fRepo;
    std::string name = Text(message, "name"), text = Text(message, "text"), hash = Text(message, "hash");
    std::vector<std::string> localNames, allNames, remoteNames;
    for (const auto& b : fModel.branches) { allNames.push_back(b.name); (b.remote ? remoteNames : localNames).push_back(b.name); }
    switch (message->what) {
        case kNewBranch: {
            if (text.empty()) {
                BMessage reply(kNewBranch);
                if (!hash.empty()) reply.AddString("hash", hash.c_str());
                Prompt("New Branch", "Branch name:", "", reply, nullptr, {}, "Create", false, {"Checkout the new branch", "Stay on the current branch"}, "After creating:");
                break;
            }
            int32 choice = 0; message->FindInt32("choice", &choice);
            bool checkout = choice == 0;
            std::string start = hash.empty() ? "HEAD" : hash;
            std::string previous = fModel.state.detached ? fModel.state.headHash : fModel.state.head;
            RunOperation("Create branch " + text, [repo, text, start, checkout](const std::atomic<bool>&) { return repo->CreateBranch(text, start, checkout); },
                [this, repo, text, checkout, previous](const ProcessResult& r) {
                    if (!r.ok()) return;
                    RecordUndo("Undo create branch " + text, [repo, text, checkout, previous] {
                        if (checkout && !previous.empty()) { auto co = repo->Checkout(previous); if (!co.ok()) return co; }
                        return repo->DeleteBranch(text, true);
                    });
                });
            break;
        }
        case kCheckoutBranch: {
            if (name.empty()) {
                if (message->HasInt32("choice")) { int32 choice = 0; message->FindInt32("choice", &choice); if (choice >= 0 && choice < static_cast<int32>(allNames.size())) CheckoutBranch(allNames[choice]); break; }
                Prompt("Checkout", "", "", BMessage(kCheckoutBranch), nullptr, {}, "Checkout", false, allNames, "Branch:");
                break;
            }
            bool isRemote = std::find(remoteNames.begin(), remoteNames.end(), name) != remoteNames.end();
            if (isRemote) { BMessage track(kTrackBranch); track.AddString("name", name.c_str()); PostMessage(&track); break; }
            CheckoutBranch(name);
            break;
        }
        case kTrackBranch: {
            Branch remote; bool found = false;
            for (const auto& b : fModel.branches) if (b.remote && b.name == name) { remote = b; found = true; }
            if (!found) break;
            std::string local = remote.shortName();
            bool exists = std::find(localNames.begin(), localNames.end(), local) != localNames.end();
            std::string previous = fModel.state.detached ? fModel.state.headHash : fModel.state.head;
            if (exists) { CheckoutBranch(local); break; }
            RunOperation("Checkout " + name + " as " + local, [repo, remote, local](const std::atomic<bool>&) { return repo->CheckoutRemoteBranch(remote, local); },
                [this, repo, local, previous](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo checkout " + local, [repo, local, previous] { auto co = repo->Checkout(previous); if (!co.ok()) return co; return repo->DeleteBranch(local, true); }); });
            break;
        }
        case kDeleteBranch: {
            if (name.empty()) {
                if (message->HasInt32("choice")) { int32 choice = 0; message->FindInt32("choice", &choice); if (choice >= 0 && choice < static_cast<int32>(localNames.size())) DeleteBranch(localNames[choice], false); break; }
                Prompt("Delete Branch", "", "", BMessage(kDeleteBranch), nullptr, {}, "Delete", false, localNames, "Branch:");
                break;
            }
            DeleteBranch(name, Flag(message, "remote"));
            break;
        }
        case kDeleteRemoteBranch: DeleteBranch(name, true); break;
        case kRenameBranch: {
            std::string from = name.empty() ? fModel.state.head : name;
            if (text.empty()) { BMessage reply(kRenameBranch); reply.AddString("name", from.c_str()); Prompt("Rename Branch", "New name:", from, reply, nullptr, {}, "Rename"); break; }
            RunOperation("Rename " + from + " to " + text, [repo, from, text](const std::atomic<bool>&) { return repo->RenameBranch(from, text); },
                [this, repo, from, text](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo rename branch", [repo, from, text] { return repo->RenameBranch(text, from); }); });
            break;
        }
        case kMergeBranch: case kSquashMerge: {
            if (name.empty()) {
                if (message->HasInt32("choice")) { int32 choice = 0; message->FindInt32("choice", &choice); if (choice >= 0 && choice < static_cast<int32>(allNames.size())) MergeInto(allNames[choice], ""); break; }
                Prompt("Merge Into " + fModel.state.head, "", "", BMessage(kMergeBranch), nullptr, {}, "Merge", false, allNames, "Merge branch:");
                break;
            }
            MergeInto(name, Text(message, "target"));
            break;
        }
        case kRebaseBranch: {
            if (name.empty()) {
                if (message->HasInt32("choice")) { int32 choice = 0; message->FindInt32("choice", &choice); if (choice >= 0 && choice < static_cast<int32>(allNames.size())) RebaseOnto("", allNames[choice]); break; }
                Prompt("Rebase " + fModel.state.head + " Onto", "", "", BMessage(kRebaseBranch), nullptr, {}, "Rebase", false, allNames, "Onto:");
                break;
            }
            RebaseOnto(Text(message, "target"), name);
            break;
        }
        case kSetUpstream: {
            std::string branch = name.empty() ? fModel.state.head : name;
            if (message->HasInt32("choice")) {
                int32 choice = 0; message->FindInt32("choice", &choice);
                std::string upstream = choice > 0 && choice <= static_cast<int32>(remoteNames.size()) ? remoteNames[choice - 1] : "";
                RunOperation("Set upstream of " + branch, [repo, branch, upstream](const std::atomic<bool>&) { return repo->SetUpstream(branch, upstream); });
                break;
            }
            std::vector<std::string> choices = {"(none)"}; choices.insert(choices.end(), remoteNames.begin(), remoteNames.end());
            BMessage reply(kSetUpstream); reply.AddString("name", branch.c_str());
            Prompt("Upstream of " + branch, "", "", reply, nullptr, {}, "Set", false, choices, "Track:");
            break;
        }
        case kCompareBranch: {
            if (name.empty()) {
                if (message->HasInt32("choice")) { int32 choice = 0; message->FindInt32("choice", &choice); if (choice >= 0 && choice < static_cast<int32>(allNames.size())) { BMessage again(kCompareBranch); again.AddString("name", allNames[choice].c_str()); PostMessage(&again); } break; }
                Prompt("Compare With " + fModel.state.head, "", "", BMessage(kCompareBranch), nullptr, {}, "Compare", false, allNames, "Branch:");
                break;
            }
            fSidebar->DeselectAll();
            fHistoryRevisions = {"HEAD.." + name};
            fHistory->SetTitle("Commits in " + name + " that are not in " + fModel.state.head);
            ShowCard(kCardHistory);
            LoadHistory(true);
            int64 request = ++fCommitRequest;
            fJobs->Submit([repo, name, request, this](const std::atomic<bool>& cancel) -> AsyncQueue::Callback {
                auto files = repo->RangeFiles("HEAD", name, &cancel);
                return [=] { if (request == fCommitRequest) fHistory->ShowRange("HEAD", name, files.value); };
            }, "commit");
            break;
        }
        case kNewTag: {
            if (text.empty()) {
                BMessage reply(kNewTag); reply.AddString("hash", hash.c_str());
                Prompt("New Tag", "Tag name:", "", reply, "Message (optional, makes an annotated tag):", {}, "Create");
                break;
            }
            std::string target = hash.empty() ? "HEAD" : hash, annotation = Text(message, "text2");
            RunOperation("Create tag " + text, [repo, text, target, annotation](const std::atomic<bool>&) { return repo->CreateTag(text, target, annotation); },
                [this, repo, text](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo create tag " + text, [repo, text] { return repo->DeleteTag(text); }); });
            break;
        }
        case kDeleteTag: {
            if (name.empty()) break;
            if (!Confirm("Delete Tag", "Delete the tag " + name + "?", "Delete")) break;
            RunOperation("Delete tag " + name, [repo, name](const std::atomic<bool>&) { return repo->DeleteTag(name); },
                [this, repo, name, hash](const ProcessResult& r) { if (r.ok() && !hash.empty()) RecordUndo("Undo delete tag " + name, [repo, name, hash] { return repo->CreateTag(name, hash, ""); }); });
            break;
        }
        case kPushTag: {
            if (name.empty() || fModel.remotes.empty()) break;
            std::string remote = fModel.remotes.front().name;
            RunOperation("Push tag " + name + " to " + remote, [repo, name, remote](const std::atomic<bool>&) { return repo->PushTag(remote, name); }, {}, true);
            break;
        }
        case kApplyStash: if (!name.empty()) ApplyStash(name, false); break;
        case kPopStash: {
            if (name.empty()) { if (fModel.stashes.empty()) { ShowError("Stash", "There are no stashes."); break; } name = fModel.stashes.front().ref; }
            ApplyStash(name, true);
            break;
        }
        case kDropStash: {
            Stash stash; bool found = false;
            for (const auto& s : fModel.stashes) if (s.ref == name) { stash = s; found = true; }
            if (!found) break;
            if (!Confirm("Drop Stash", "Drop the stash \"" + stash.message + "\"?", "Drop")) break;
            RunOperation("Drop stash", [repo, stash](const std::atomic<bool>&) { return repo->StashDrop(stash); },
                [this, repo, stash](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo drop stash", [repo, stash] { return repo->Run({"stash", "store", "-m", stash.message, stash.hash}); }); });
            break;
        }
        case kSaveStash: {
            if (!message->HasString("text")) {
                if (fFiles.empty()) { ShowError("Stash", "There are no changes to stash."); break; }
                Prompt("Stash Changes", "Message:", "", BMessage(kSaveStash), nullptr, {}, "Stash", false, {"Tracked changes only", "Include untracked files"}, "Include:");
                break;
            }
            int32 choice = 0; message->FindInt32("choice", &choice);
            bool untracked = choice == 1;
            RunOperation("Stash changes", [repo, text, untracked](const std::atomic<bool>&) { return repo->StashSave(text, untracked, false); },
                [this, repo](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo stash", [repo] { Stash top; top.ref = "stash@{0}"; return repo->StashApply(top, true); }); });
            break;
        }
        default: break;
    }
}
// ---------------------------------------------------------------- history
void RepositoryWindow::HandleHistory(BMessage* message) {
    auto repo = fRepo;
    std::vector<std::string> hashes;
    const char* value;
    for (int32 i = 0; message->FindString("hash", i, &value) == B_OK; ++i) if (std::find(hashes.begin(), hashes.end(), value) == hashes.end()) hashes.push_back(value);
    std::string hash = hashes.empty() ? "" : hashes.front();
    switch (message->what) {
        case kCheckoutCommit: {
            if (hash.empty()) break;
            std::string branch = BranchAt(hash);
            if (!branch.empty()) { CheckoutBranch(branch); break; }
            if (!Confirm("Checkout Commit", "Checkout " + Short(hash) + " as a detached HEAD? Create a branch from it to keep new commits.", "Checkout", false)) break;
            CheckoutBranch(hash);
            break;
        }
        case kBranchHere: { BMessage reply(kNewBranch); reply.AddString("hash", hash.c_str()); Prompt("New Branch at " + Short(hash), "Branch name:", "", reply, nullptr, {}, "Create", false, {"Checkout the new branch", "Stay on the current branch"}, "After creating:"); break; }
        case kTagHere: { BMessage reply(kNewTag); reply.AddString("hash", hash.c_str()); Prompt("New Tag at " + Short(hash), "Tag name:", "", reply, "Message (optional, makes an annotated tag):", {}, "Create"); break; }
        case kCherryPick: if (!hashes.empty()) CherryPickOnto(hashes, ""); break;
        case kRevertCommit: {
            if (hash.empty()) break;
            std::string oldHead = fModel.state.headHash;
            RunOperation("Revert " + Short(hash), [repo, hash](const std::atomic<bool>&) { return repo->Revert(hash); },
                [this, repo, oldHead, hash](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo revert " + Short(hash), [repo, oldHead] { return repo->Reset(oldHead, "hard"); }); });
            break;
        }
        case kResetSoft: case kResetMixed: case kResetHard: {
            if (hash.empty()) break;
            std::string mode = message->what == kResetSoft ? "soft" : message->what == kResetMixed ? "mixed" : "hard";
            if (fModel.state.detached) { ShowError("Reset", "Checkout a branch before resetting."); break; }
            std::string warning = "Reset " + fModel.state.head + " to " + Short(hash) + " (" + mode + ")?";
            if (mode == "hard") warning += "\n\nUncommitted changes are discarded. Undo restores them while this window is open.";
            if (!Confirm("Reset Branch", warning, "Reset")) break;
            std::string oldHead = fModel.state.headHash;
            RunOperation("Reset to " + Short(hash), [repo, hash, mode](const std::atomic<bool>&) {
                std::string backup = mode == "soft" ? "" : repo->CreateBackup();
                auto result = repo->Reset(hash, mode);
                if (result.ok()) result.output = backup;
                return result;
            }, [this, repo, oldHead, mode, hash](const ProcessResult& r) {
                if (!r.ok()) return;
                std::string backup = Trim(r.output);
                RecordUndo("Undo reset to " + Short(hash), [repo, oldHead, mode, backup] {
                    if (mode == "soft") return repo->Reset(oldHead, "soft");
                    auto reset = repo->Reset(oldHead, "hard");
                    if (reset.ok() && ValidHash(backup)) return repo->RestoreBackup(backup);
                    return reset;
                });
            });
            break;
        }
        case kCopyHash: {
            if (hash.empty()) break;
            if (be_clipboard->Lock()) {
                be_clipboard->Clear();
                if (BMessage* clip = be_clipboard->Data()) { clip->AddData("text/plain", B_MIME_TYPE, hash.c_str(), hash.size()); be_clipboard->Commit(); }
                be_clipboard->Unlock();
            }
            SetStatus("Copied " + hash);
            break;
        }
        case kCompareCommits: break;   // two selected commits already show as a range
        default: break;
    }
}
// ---------------------------------------------------------------- remotes
void RepositoryWindow::HandleRemote(BMessage* message) {
    auto repo = fRepo;
    std::string name = Text(message, "name");
    bool prune = true, pullRebase = false;
    { BAutolock lock(App()->SettingsLock()); prune = App()->Preferences().GetBool("fetchPrune", true); pullRebase = App()->Preferences().GetBool("pullRebase", false); }
    const Branch* current = nullptr;
    for (const auto& b : fModel.branches) if (b.current) current = &b;
    switch (message->what) {
        case kFetchAll: RunOperation("Fetch all remotes", [repo, prune](const std::atomic<bool>& cancel) { return repo->FetchAll(prune, &cancel); }, {}, true); break;
        case kFetch: RunOperation("Fetch " + name, [repo, name, prune](const std::atomic<bool>& cancel) { return repo->Fetch(name, prune, &cancel); }, {}, true); break;
        case kPull: {
            if (!current) { ShowError("Pull", "Checkout a branch first."); break; }
            if (current->upstream.empty()) { ShowError("Pull", current->name + " has no upstream branch. Set one from the branch's context menu."); break; }
            std::string oldHead = fModel.state.headHash;
            RunOperation("Pull " + current->upstream, [repo, pullRebase](const std::atomic<bool>& cancel) { return repo->Pull("", "", pullRebase, &cancel); },
                [this, repo, oldHead](const ProcessResult& r) { if (r.ok() && !oldHead.empty()) RecordUndo("Undo pull", [repo, oldHead] { return repo->Reset(oldHead, "hard"); }); }, true);
            break;
        }
        case kPush: {
            if (!current) { ShowError("Push", "Checkout a branch first."); break; }
            std::string remote = current->upstream.empty() ? (fModel.remotes.empty() ? "" : fModel.remotes.front().name) : current->upstream.substr(0, current->upstream.find('/'));
            if (remote.empty()) { ShowError("Push", "Add a remote first (Repository > Add Remote)."); break; }
            PushBranch(current->name, remote, Flag(message, "force"));
            break;
        }
        case kPushBranch: {
            std::string remote = Text(message, "remote");
            if (remote.empty()) remote = fModel.remotes.empty() ? "" : fModel.remotes.front().name;
            if (name.empty() || remote.empty()) break;
            PushBranch(name, remote, Flag(message, "force"));
            break;
        }
        case kPullBranch: {
            // Merge a remote branch into the current branch (a targeted pull).
            if (name.empty() || !current) break;
            MergeInto(name, "");
            break;
        }
        case kAddRemote: {
            std::string text = Text(message, "text"), url = Text(message, "text2");
            if (text.empty()) { Prompt("Add Remote", "Name:", fModel.remotes.empty() ? "origin" : "", BMessage(kAddRemote), "URL:", {}, "Add"); break; }
            RunOperation("Add remote " + text, [repo, text, url](const std::atomic<bool>&) { return repo->AddRemote(text, url); },
                [this, repo, text](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo add remote " + text, [repo, text] { return repo->RemoveRemote(text); }); });
            break;
        }
        case kRemoveRemote: {
            Remote remote; bool found = false;
            for (const auto& r : fModel.remotes) if (r.name == name) { remote = r; found = true; }
            if (!found || !Confirm("Remove Remote", "Remove the remote " + name + " and its remote-tracking branches?", "Remove")) break;
            RunOperation("Remove remote " + name, [repo, name](const std::atomic<bool>&) { return repo->RemoveRemote(name); },
                [this, repo, remote](const ProcessResult& r) { if (r.ok()) RecordUndo("Undo remove remote " + remote.name, [repo, remote] { return repo->AddRemote(remote.name, remote.fetchURL); }); });
            break;
        }
        case kUpdateSubmodules: RunOperation("Update submodules", [repo](const std::atomic<bool>& cancel) { return repo->SubmoduleUpdate(&cancel); }, {}, true); break;
        default: break;
    }
}
// ---------------------------------------------------------------- pull requests
void RepositoryWindow::HandlePullRequests(BMessage* message) {
    auto repo = fRepo;
    int32 number = 0; message->FindInt32("number", &number);
    const PullRequest* pr = nullptr;
    for (const auto& p : fPullRequestList) if (p.number == number) pr = &p;
    std::string originName = "origin";
    for (const auto& r : fModel.remotes) if (GitHubRepository(r.fetchURL).value_or("") == fGitHubRepository) { originName = r.name; break; }
    switch (message->what) {
        case kPullRequestsRefresh: LoadPullRequests(); break;
        case kPullRequestSelected: if (number > 0) LoadPullRequest(number); break;
        case kPullRequestOpenBrowser: if (pr) OpenExternally(pr->url); break;
        case kPullRequestCheckout: {
            if (!pr) break;
            std::string local = "pr/" + std::to_string(number);
            std::string previous = fModel.state.detached ? fModel.state.headHash : fModel.state.head;
            int n = number;
            RunOperation("Checkout pull request #" + std::to_string(number), [repo, originName, n, local](const std::atomic<bool>& cancel) { return repo->FetchPullRequest(originName, n, local, &cancel); },
                [this, repo, local, previous](const ProcessResult& r) {
                    if (!r.ok()) return;
                    RecordUndo("Undo checkout " + local, [repo, local, previous] { auto co = repo->Checkout(previous); if (!co.ok()) return co; return repo->DeleteBranch(local, true); });
                    fSidebar->SelectKind(SidebarEntry::kWorkingCopy);
                }, true);
            break;
        }
        case kPullRequestMerge: {
            if (!pr) break;
            if (!message->HasInt32("choice")) {
                BMessage reply(kPullRequestMerge); reply.AddInt32("number", number);
                Prompt("Merge Pull Request #" + std::to_string(number), "", "", reply, nullptr, {}, "Merge", false, {"Create a merge commit", "Squash and merge", "Rebase and merge"}, "Method:");
                break;
            }
            int32 choice = 0; message->FindInt32("choice", &choice);
            std::string method = choice == 1 ? "squash" : choice == 2 ? "rebase" : "merge";
            auto client = std::shared_ptr<GitHubClient>(GitHub().release());
            int n = number;
            RunOperation("Merge pull request #" + std::to_string(number), [client, n, method](const std::atomic<bool>& cancel) {
                auto result = client->MergePullRequest(n, method, &cancel);
                ProcessResult r; r.exitCode = result.ok() ? 0 : 1; r.error = result.error; return r;
            }, [this](const ProcessResult& r) { if (r.ok()) LoadPullRequests(); }, true);
            break;
        }
        case kPullRequestClose: {
            if (!pr) break;
            bool reopen = pr->state == "closed";
            if (!reopen && !Confirm("Close Pull Request", "Close pull request #" + std::to_string(number) + " without merging?", "Close")) break;
            auto client = std::shared_ptr<GitHubClient>(GitHub().release());
            int n = number;
            RunOperation((reopen ? "Reopen" : "Close") + std::string(" pull request #") + std::to_string(number), [client, n, reopen](const std::atomic<bool>& cancel) {
                auto result = client->ClosePullRequest(n, reopen, &cancel);
                ProcessResult r; r.exitCode = result.ok() ? 0 : 1; r.error = result.error; return r;
            }, [this](const ProcessResult& r) { if (r.ok()) LoadPullRequests(); }, true);
            break;
        }
        case kPullRequestComment: {
            if (!pr) break;
            std::string body = Text(message, "text");
            if (body.empty()) { BMessage reply(kPullRequestComment); reply.AddInt32("number", number); Prompt("Comment on #" + std::to_string(number), "Comment:", "", reply, nullptr, {}, "Post", true); break; }
            auto client = std::shared_ptr<GitHubClient>(GitHub().release());
            int n = number;
            RunOperation("Post comment", [client, n, body](const std::atomic<bool>& cancel) {
                auto result = client->AddComment(n, body, &cancel);
                ProcessResult r; r.exitCode = result.ok() ? 0 : 1; r.error = result.error; return r;
            }, [this, n](const ProcessResult& r) { if (r.ok()) LoadPullRequest(n); }, true);
            break;
        }
        case kNewPullRequest: {
            if (fGitHubRepository.empty()) { ShowError("Pull Request", "This repository has no GitHub remote."); break; }
            if (App()->GitHubToken().empty()) { ShowError("Pull Request", "Add a GitHub personal access token in Preferences to create pull requests."); break; }
            if (fModel.state.detached) { ShowError("Pull Request", "Checkout the branch you want to propose first."); break; }
            std::vector<std::string> bases;
            for (const auto& b : fModel.branches) if (b.remote && b.remoteName() == originName) bases.push_back(b.shortName());
            if (bases.empty()) bases = {"main", "master"};
            std::sort(bases.begin(), bases.end(), [](const std::string& a, const std::string& b) { return (a == "main" || a == "master") && !(b == "main" || b == "master") ? true : (b == "main" || b == "master") ? false : a < b; });
            BMessage reply(kSubmitPullRequest);
            std::string title = fPreviousCommitMessage.substr(0, fPreviousCommitMessage.find('\n'));
            Prompt("New Pull Request from " + fModel.state.head, "Title:", title, reply, "Description:", {}, "Create", true, bases, "Base branch:");
            break;
        }
        case kSubmitPullRequest: {
            std::string title = Text(message, "text"), body = Text(message, "text2");
            int32 choice = 0; message->FindInt32("choice", &choice);
            std::vector<std::string> bases;
            for (const auto& b : fModel.branches) if (b.remote && b.remoteName() == originName) bases.push_back(b.shortName());
            if (bases.empty()) bases = {"main", "master"};
            std::sort(bases.begin(), bases.end(), [](const std::string& a, const std::string& b) { return (a == "main" || a == "master") && !(b == "main" || b == "master") ? true : (b == "main" || b == "master") ? false : a < b; });
            std::string base = choice >= 0 && choice < static_cast<int32>(bases.size()) ? bases[choice] : bases.front();
            std::string head = fModel.state.head;
            if (Trim(title).empty()) { ShowError("Pull Request", "Enter a title."); break; }
            auto client = std::shared_ptr<GitHubClient>(GitHub().release());
            bool needsPush = true;
            for (const auto& b : fModel.branches) if (b.current && !b.upstream.empty() && b.ahead == 0) needsPush = false;
            RunOperation("Create pull request", [repo, client, title, body, head, base, originName, needsPush, this](const std::atomic<bool>& cancel) {
                if (needsPush) { auto push = repo->Push(originName, head, true, false, &cancel); if (!push.ok()) return push; }
                auto result = client->CreatePullRequest(title, body, head, base, false, &cancel);
                ProcessResult r; r.exitCode = result.ok() ? 0 : 1; r.error = result.error; r.output = result.value.url; return r;
            }, [this](const ProcessResult& r) {
                if (!r.ok()) return;
                fSidebar->SelectKind(SidebarEntry::kPullRequests);
                LoadPullRequests();
                if (!r.output.empty()) OpenExternally(r.output);
            }, true);
            break;
        }
        default: break;
    }
}
// ---------------------------------------------------------------- sidebar context menus and double-clicks
void RepositoryWindow::HandleSidebarInvoke(BMessage* message) {
    int32 index = -1; message->FindInt32("index", &index);
    if (index < 0) index = fSidebar->CurrentSelection();
    const SidebarEntry* entry = fSidebar->EntryAt(index);
    if (!entry) return;
    SidebarEntry e = *entry;
    bool context = Flag(message, "context"), remove = Flag(message, "delete");
    BPoint screen; message->FindPoint("screen", &screen);
    auto send = [this](uint32 what, const SidebarEntry& target, const char* extraName = nullptr, const char* extraValue = nullptr) {
        BMessage m(what);
        m.AddString("name", target.name.c_str()); m.AddString("hash", target.hash.c_str());
        m.AddBool("remote", target.kind == SidebarEntry::kRemoteBranch);
        if (extraName) m.AddString(extraName, extraValue);
        PostMessage(&m);
    };
    if (remove) {
        if (e.kind == SidebarEntry::kBranch || e.kind == SidebarEntry::kRemoteBranch) send(kDeleteBranch, e);
        else if (e.kind == SidebarEntry::kTag) send(kDeleteTag, e);
        else if (e.kind == SidebarEntry::kStash) send(kDropStash, e);
        return;
    }
    if (!context) {
        switch (e.kind) {
            case SidebarEntry::kBranch: if (!e.current) CheckoutBranch(e.name); break;
            case SidebarEntry::kRemoteBranch: send(kTrackBranch, e); break;
            case SidebarEntry::kTag: if (Confirm("Checkout Tag", "Checkout tag " + e.name + " as a detached HEAD?", "Checkout", false)) CheckoutBranch(e.name); break;
            case SidebarEntry::kStash: if (Confirm("Apply Stash", "Apply \"" + e.label + "\" to the working copy? The stash is kept.", "Apply", false)) ApplyStash(e.name, false); break;
            default: break;
        }
        return;
    }
    std::vector<MenuChoice> choices;
    std::string current = fModel.state.head;
    switch (e.kind) {
        case SidebarEntry::kBranch:
            choices = {{"Checkout " + e.name, kCheckoutBranch, !e.current},
                       {"Merge " + e.name + " Into " + current + B_UTF8_ELLIPSIS, kMergeBranch, !e.current},
                       {"Rebase " + current + " Onto " + e.name + B_UTF8_ELLIPSIS, kRebaseBranch, !e.current},
                       {"Compare With " + current, kCompareBranch, !e.current},
                       {"Push " + e.name + B_UTF8_ELLIPSIS, kPushBranch, true, true},
                       {"Force Push " + e.name + B_UTF8_ELLIPSIS, 'fpsh'},
                       {"Set Upstream" B_UTF8_ELLIPSIS, kSetUpstream},
                       {"New Branch From Here" B_UTF8_ELLIPSIS, kBranchHere, true, true},
                       {"New Tag Here" B_UTF8_ELLIPSIS, kTagHere},
                       {"Rename" B_UTF8_ELLIPSIS, kRenameBranch, true, true},
                       {"Delete" B_UTF8_ELLIPSIS, kDeleteBranch, !e.current}};
            break;
        case SidebarEntry::kRemoteBranch:
            choices = {{"Checkout as Local Branch", kTrackBranch},
                       {"Merge Into " + current + B_UTF8_ELLIPSIS, kPullBranch},
                       {"Rebase " + current + " Onto " + e.name + B_UTF8_ELLIPSIS, kRebaseBranch},
                       {"Compare With " + current, kCompareBranch},
                       {"New Branch From Here" B_UTF8_ELLIPSIS, kBranchHere, true, true},
                       {"Delete on Remote" B_UTF8_ELLIPSIS, kDeleteRemoteBranch, true, true}};
            break;
        case SidebarEntry::kRemote:
            choices = {{"Fetch " + e.name, kFetch}, {"Push " + current + " to " + e.name, kPushBranch, !fModel.state.detached},
                       {"Open URL", 'open', e.detail.rfind("http", 0) == 0, true}, {"Remove Remote" B_UTF8_ELLIPSIS, kRemoveRemote, true, true}};
            break;
        case SidebarEntry::kTag:
            choices = {{"Checkout (detached)", kCheckoutCommit}, {"New Branch From Here" B_UTF8_ELLIPSIS, kBranchHere},
                       {"Push Tag to " + (fModel.remotes.empty() ? std::string("remote") : fModel.remotes.front().name), kPushTag, !fModel.remotes.empty(), true},
                       {"Delete Tag" B_UTF8_ELLIPSIS, kDeleteTag, true, true}};
            break;
        case SidebarEntry::kStash:
            choices = {{"Apply (keep stash)", kApplyStash}, {"Pop (apply and drop)", kPopStash}, {"Drop" B_UTF8_ELLIPSIS, kDropStash, true, true}};
            break;
        case SidebarEntry::kWorkingCopy:
            choices = {{"Stash Changes" B_UTF8_ELLIPSIS, kSaveStash, !fFiles.empty()}, {"Stage All", kStageAll, !fFiles.empty()}, {"Discard All Changes" B_UTF8_ELLIPSIS, kDiscardAll, !fFiles.empty()},
                       {"Show in Tracker", kOpenInTracker, true, true}, {"Open Terminal Here", kOpenTerminal}};
            break;
        case SidebarEntry::kPullRequests: choices = {{"Refresh", kPullRequestsRefresh}, {"New Pull Request" B_UTF8_ELLIPSIS, kNewPullRequest}}; break;
        case SidebarEntry::kSubmodule: choices = {{"Open in Turbo Chook", 'opsm'}, {"Update Submodules", kUpdateSubmodules}}; break;
        default: return;
    }
    uint32 command = PopupChoice(fSidebar, screen, choices);
    if (!command) return;
    if (command == 'fpsh') { BMessage m(kPushBranch); m.AddString("name", e.name.c_str()); m.AddBool("force", true); PostMessage(&m); return; }
    if (command == 'open') { OpenExternally(e.detail); return; }
    if (command == 'opsm') { App()->OpenRepository(fRoot + "/" + e.name); return; }
    if (command == kCheckoutCommit) { BMessage m(kCheckoutCommit); m.AddString("hash", e.hash.c_str()); PostMessage(&m); return; }
    if (command == kBranchHere || command == kTagHere) { BMessage m(command); m.AddString("hash", e.hash.c_str()); PostMessage(&m); return; }
    if (command == kPushBranch && e.kind == SidebarEntry::kRemote) { BMessage m(kPushBranch); m.AddString("name", current.c_str()); m.AddString("remote", e.name.c_str()); PostMessage(&m); return; }
    send(command, e);
}
// ---------------------------------------------------------------- drag & drop
void RepositoryWindow::HandleDrop(BMessage* message) {
    BMessage payload;
    if (message->FindMessage("payload", &payload) != B_OK) return;
    int32 kindValue = -1; message->FindInt32("kind", &kindValue);
    std::string targetName = Text(message, "name"), targetLabel = Text(message, "label"), targetHash = Text(message, "hash");
    BPoint screen; message->FindPoint("screen", &screen);
    SidebarEntry::Kind kind = kindValue < 0 ? SidebarEntry::kSection : static_cast<SidebarEntry::Kind>(kindValue);
    std::string current = fModel.state.head;
    std::string source = Text(&payload, "name"), sourceLabel = Text(&payload, "label"), sourceHash = Text(&payload, "hash");
    bool sourceRemote = Flag(&payload, "remote");
    std::vector<MenuChoice> choices;
    enum { kDoMerge = 1, kDoRebase, kDoCompare, kDoPush, kDoForcePush, kDoCheckout, kDoTrack, kDoCherry, kDoBranch, kDoTag, kDoApply, kDoPop, kDoPushTag, kDoMergeRemote };
    if (payload.what == kDragBranch) {
        if (kind == SidebarEntry::kBranch && targetName != source) {
            choices = {{"Merge " + source + " into " + targetName, kDoMerge},
                       {"Rebase " + targetName + " onto " + source, kDoRebase},
                       {"Compare " + source + " with " + targetName, kDoCompare, true, true}};
        } else if (kind == SidebarEntry::kRemote && !sourceRemote) {
            choices = {{"Push " + source + " to " + targetName, kDoPush}, {"Force push " + source + " to " + targetName, kDoForcePush}};
        } else if (kind == SidebarEntry::kWorkingCopy || kind == SidebarEntry::kHistoryCurrent) {
            if (sourceRemote) choices = {{"Checkout " + source + " as a local branch", kDoTrack}, {"Merge " + source + " into " + current, kDoMerge}};
            else choices = {{"Checkout " + source, kDoCheckout, source != current}, {"Merge " + source + " into " + current, kDoMerge, source != current}, {"Rebase " + current + " onto " + source, kDoRebase, source != current}};
        } else if (kind == SidebarEntry::kSection && targetLabel == "Branches" && sourceRemote) {
            choices = {{"Create local branch tracking " + source, kDoTrack}};
        } else if (kind == SidebarEntry::kRemoteBranch && !sourceRemote) {
            choices = {{"Merge " + targetName + " into " + source, kDoMergeRemote}, {"Compare " + source + " with " + targetName, kDoCompare}};
        }
    } else if (payload.what == kDragCommit) {
        std::vector<std::string> hashes; const char* h;
        for (int32 i = 0; payload.FindString("hash", i, &h) == B_OK; ++i) hashes.push_back(h);
        std::string what = hashes.size() == 1 ? "commit " + Short(hashes.front()) : std::to_string(hashes.size()) + " commits";
        if (kind == SidebarEntry::kBranch) choices = {{"Cherry-pick " + what + " onto " + targetName, kDoCherry}};
        else if (kind == SidebarEntry::kWorkingCopy || kind == SidebarEntry::kHistoryCurrent) choices = {{"Cherry-pick " + what + " into " + current, kDoCherry}};
        else if (kind == SidebarEntry::kSection && targetLabel == "Branches" && hashes.size() == 1) choices = {{"Create branch at " + Short(hashes.front()) + B_UTF8_ELLIPSIS, kDoBranch}};
        else if (kind == SidebarEntry::kSection && targetLabel == "Tags" && hashes.size() == 1) choices = {{"Create tag at " + Short(hashes.front()) + B_UTF8_ELLIPSIS, kDoTag}};
        if (choices.empty()) return;
        uint32 command = PopupChoice(fSidebar, screen, choices);
        if (command == kDoCherry) CherryPickOnto(hashes, kind == SidebarEntry::kBranch ? targetName : "");
        else if (command == kDoBranch) { BMessage m(kBranchHere); m.AddString("hash", hashes.front().c_str()); PostMessage(&m); }
        else if (command == kDoTag) { BMessage m(kTagHere); m.AddString("hash", hashes.front().c_str()); PostMessage(&m); }
        return;
    } else if (payload.what == kDragStash) {
        if (kind == SidebarEntry::kWorkingCopy || kind == SidebarEntry::kHistoryCurrent || kind == SidebarEntry::kBranch)
            choices = {{"Apply stash to the working copy", kDoApply}, {"Pop stash (apply and drop)", kDoPop}};
    } else if (payload.what == kDragTag) {
        if (kind == SidebarEntry::kWorkingCopy || kind == SidebarEntry::kHistoryCurrent) choices = {{"Checkout tag " + source + " (detached)", kDoCheckout}, {"Create branch from " + source + B_UTF8_ELLIPSIS, kDoBranch}};
        else if (kind == SidebarEntry::kRemote) choices = {{"Push tag " + source + " to " + targetName, kDoPushTag}};
        else if (kind == SidebarEntry::kSection && targetLabel == "Branches") choices = {{"Create branch from " + source + B_UTF8_ELLIPSIS, kDoBranch}};
    }
    if (choices.empty()) return;
    uint32 command = PopupChoice(fSidebar, screen, choices);
    switch (command) {
        case kDoMerge: MergeInto(source, kind == SidebarEntry::kBranch ? targetName : ""); break;
        case kDoMergeRemote: MergeInto(targetName, source); break;
        case kDoRebase: RebaseOnto(kind == SidebarEntry::kBranch ? targetName : "", source); break;
        case kDoCompare: { BMessage m(kCompareBranch); m.AddString("name", source.c_str()); PostMessage(&m); break; }
        case kDoPush: PushBranch(source, targetName, false); break;
        case kDoForcePush: PushBranch(source, targetName, true); break;
        case kDoCheckout: CheckoutBranch(source); break;
        case kDoTrack: { BMessage m(kTrackBranch); m.AddString("name", source.c_str()); PostMessage(&m); break; }
        case kDoApply: ApplyStash(source, false); break;
        case kDoPop: ApplyStash(source, true); break;
        case kDoPushTag: { BMessage m(kPushTag); m.AddString("name", source.c_str()); PostMessage(&m); break; }
        case kDoBranch: { BMessage m(kBranchHere); m.AddString("hash", sourceHash.c_str()); PostMessage(&m); break; }
        default: break;
    }
}
}
