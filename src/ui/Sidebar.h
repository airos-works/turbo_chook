#pragma once
#include "core/Git.h"
#include "ui/Widgets.h"
#include <Messenger.h>
#include <OutlineListView.h>
#include <string>
#include <vector>
namespace tc {
// One entry in the navigation tree. Kinds decide the icon, the badge and
// which drag & drop operations apply.
struct SidebarEntry {
    enum Kind { kSection, kWorkingCopy, kHistoryAll, kHistoryCurrent, kBranch, kRemote, kRemoteBranch, kTag, kStash, kPullRequests, kSubmodule };
    Kind kind = kSection;
    std::string label, name, hash, detail;   // name: branch/tag/stash ref, detail: subtitle
    int ahead = 0, behind = 0, count = 0;
    bool current = false, gone = false;
};
class SidebarItem : public BListItem {
public:
    explicit SidebarItem(SidebarEntry entry, uint32 level = 0, bool expanded = true);
    void DrawItem(BView* owner, BRect frame, bool complete) override;
    void Update(BView* owner, const BFont* font) override;
    const SidebarEntry& Entry() const { return fEntry; }
    SidebarEntry& Entry() { return fEntry; }
private:
    SidebarEntry fEntry;
};
struct SidebarModel {
    std::vector<Branch> branches;
    std::vector<Tag> tags;
    std::vector<Stash> stashes;
    std::vector<Remote> remotes;
    std::vector<Submodule> submodules;
    RepositoryState state;
    int changes = 0;
    bool hasGitHub = false;
    int pullRequests = -1;
    // Everything that affects what the sidebar shows; unchanged models skip the rebuild.
    std::string Signature() const;
};
// Outline list with sections, drag sources (branches, tags, stashes) and drop
// targets. Drops post kSidebarDrop to the target with the dragged payload and
// the destination entry.
class Sidebar : public BOutlineListView {
public:
    Sidebar();
    void SetDropTarget(BMessenger target) { fTarget = target; }
    void SetModel(const SidebarModel& model);
    const SidebarEntry* Selected() const;
    const SidebarEntry* EntryAt(int32 index) const;
    void SelectKind(SidebarEntry::Kind kind, const std::string& name = {});
    bool InitiateDrag(BPoint point, int32 index, bool wasSelected) override;
    void MouseDown(BPoint where) override;
    void MouseMoved(BPoint where, uint32 transit, const BMessage* drag) override;
    void MouseUp(BPoint where) override;
    void MessageReceived(BMessage* message) override;
    void Draw(BRect updateRect) override;
    void KeyDown(const char* bytes, int32 count) override;
private:
    void AddSection(const char* label, SidebarEntry::Kind kind, std::vector<SidebarEntry> children, bool keepEmpty = true);
    SidebarItem* Section(const std::string& label) const;
    std::vector<std::string> fCollapsed, fExpanded;   // explicit user choices survive rebuilds
    std::string fSignature;
    BMessenger fTarget;
    int32 fDropIndex = -1;
    Palette fPalette;
};
}
