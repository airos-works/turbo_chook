#include "ui/Sidebar.h"
#include "ui/Messages.h"
#include <Bitmap.h>
#include <Window.h>
#include <algorithm>
#include <cmath>

namespace tc {
SidebarItem::SidebarItem(SidebarEntry entry, uint32 level, bool expanded) : BListItem(level, expanded), fEntry(std::move(entry)) {}
void SidebarItem::Update(BView* owner, const BFont* font) {
    BListItem::Update(owner, font);
    font_height fh; font->GetHeight(&fh);
    SetHeight(std::ceil(fh.ascent + fh.descent) + (fEntry.kind == SidebarEntry::kSection ? 12 : 8));
}
void SidebarItem::DrawItem(BView* owner, BRect frame, bool) {
    Palette palette = Palette::Current();
    rgb_color base = ui_color(B_LIST_BACKGROUND_COLOR);
    rgb_color text = ui_color(B_LIST_ITEM_TEXT_COLOR);
    bool section = fEntry.kind == SidebarEntry::kSection;
    if (IsSelected() && !section) { base = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR); text = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR); }
    owner->SetHighColor(base); owner->FillRect(frame);
    font_height fh; be_plain_font->GetHeight(&fh);
    float baseline = frame.top + (frame.Height() - (fh.ascent + fh.descent)) / 2 + fh.ascent;
    float x = frame.left + 6 + OutlineLevel() * 14;
    if (section) {
        BFont small(be_bold_font); small.SetSize(std::max(8.0f, be_bold_font->Size() * 0.85f));
        owner->SetFont(&small); owner->SetHighColor(palette.muted);
        std::string label = fEntry.label;
        for (auto& c : label) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        owner->DrawString(label.c_str(), BPoint(x + 12, baseline));
        if (fEntry.count > 0) {
            std::string count = std::to_string(fEntry.count);
            owner->DrawString(count.c_str(), BPoint(frame.right - 8 - small.StringWidth(count.c_str()), baseline));
        }
        owner->SetFont(be_plain_font);
        return;
    }
    // Icon glyph: a small coloured marker per kind.
    rgb_color marker = palette.badge;
    switch (fEntry.kind) {
        case SidebarEntry::kBranch: marker = fEntry.current ? palette.current : palette.badge; break;
        case SidebarEntry::kRemoteBranch: case SidebarEntry::kRemote: marker = palette.remote; break;
        case SidebarEntry::kTag: marker = palette.tag; break;
        case SidebarEntry::kStash: marker = palette.stash; break;
        case SidebarEntry::kWorkingCopy: marker = fEntry.count > 0 ? palette.conflict : palette.badge; break;
        default: break;
    }
    BRect dot(x, baseline - fh.ascent + 2, x + 8, baseline - fh.ascent + 10);
    owner->SetHighColor(Mix(marker, text, 0.2f));
    if (fEntry.kind == SidebarEntry::kTag) { owner->FillTriangle(dot.LeftTop(), dot.RightTop(), BPoint((dot.left + dot.right) / 2, dot.bottom)); }
    else if (fEntry.kind == SidebarEntry::kStash) owner->FillRect(dot);
    else if (fEntry.kind == SidebarEntry::kBranch || fEntry.kind == SidebarEntry::kRemoteBranch) { owner->FillEllipse(dot); }
    else owner->FillRoundRect(dot, 2, 2);
    x += 14;
    owner->SetFont(fEntry.current ? be_bold_font : be_plain_font);
    owner->SetHighColor(fEntry.gone ? Mix(text, base, 0.5f) : text);
    float right = frame.right - 6;
    // Badges from the right: ahead/behind counters or counts.
    BFont small(be_plain_font); small.SetSize(std::max(8.0f, be_plain_font->Size() * 0.8f));
    auto badge = [&](const std::string& value, rgb_color fill) {
        owner->SetFont(&small);
        float w = small.StringWidth(value.c_str()) + 8;
        BRect box(right - w, frame.top + 3, right, frame.bottom - 3);
        owner->SetHighColor(fill); owner->FillRoundRect(box, 4, 4);
        owner->SetHighColor(DarkUI() ? rgb_color{235, 235, 235, 255} : rgb_color{35, 35, 35, 255});
        owner->DrawString(value.c_str(), BPoint(box.left + 4, baseline - 1));
        right = box.left - 4;
    };
    if (fEntry.behind > 0) badge("\xe2\x86\x93" + std::to_string(fEntry.behind), palette.remote);
    if (fEntry.ahead > 0) badge("\xe2\x86\x91" + std::to_string(fEntry.ahead), palette.current);
    if (fEntry.gone) badge("gone", palette.conflict);
    if (fEntry.kind == SidebarEntry::kWorkingCopy && fEntry.count > 0) badge(std::to_string(fEntry.count), palette.badge);
    if (fEntry.kind == SidebarEntry::kPullRequests && fEntry.count > 0) badge(std::to_string(fEntry.count), palette.badge);
    owner->SetFont(fEntry.current ? be_bold_font : be_plain_font);
    owner->SetHighColor(fEntry.gone ? Mix(text, base, 0.5f) : text);
    BString label(fEntry.label.c_str());
    (fEntry.current ? be_bold_font : be_plain_font)->TruncateString(&label, B_TRUNCATE_MIDDLE, std::max(30.0f, right - x));
    owner->DrawString(label.String(), BPoint(x, baseline));
    owner->SetFont(be_plain_font);
}
Sidebar::Sidebar() : BOutlineListView("sidebar", B_SINGLE_SELECTION_LIST, B_WILL_DRAW | B_NAVIGABLE | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE), fPalette(Palette::Current()) {
    SetSelectionMessage(new BMessage(kSidebarSelection));
    SetInvocationMessage(new BMessage(kSidebarInvoke));
}
SidebarItem* Sidebar::Section(const std::string& label) const {
    for (int32 i = 0; i < FullListCountItems(); ++i) {
        auto item = static_cast<SidebarItem*>(FullListItemAt(i));
        if (item->Entry().kind == SidebarEntry::kSection && item->Entry().label == label) return item;
    }
    return nullptr;
}
void Sidebar::AddSection(const char* label, SidebarEntry::Kind kind, std::vector<SidebarEntry> children, bool keepEmpty) {
    if (children.empty() && !keepEmpty) return;
    SidebarEntry header; header.kind = SidebarEntry::kSection; header.label = label; header.count = static_cast<int>(children.size());
    bool expanded = std::find(fCollapsed.begin(), fCollapsed.end(), label) == fCollapsed.end();
    auto section = new SidebarItem(header, 0, expanded);
    AddItem(section);
    // AddUnder inserts directly below the section, so add in reverse to keep the order.
    for (auto it = children.rbegin(); it != children.rend(); ++it) AddUnder(new SidebarItem(*it, 1), section);
    (void)kind;
}
std::string SidebarModel::Signature() const {
    std::string s = state.head + '|' + state.headHash + '|' + std::to_string(state.inProgress()) + '|' + std::to_string(changes) + '|'
        + std::to_string(hasGitHub) + '|' + std::to_string(pullRequests) + '\n';
    for (const auto& b : branches) s += b.fullRef + ' ' + b.hash + ' ' + b.upstream + ' ' + std::to_string(b.ahead) + ' ' + std::to_string(b.behind) + ' ' + std::to_string(b.current) + std::to_string(b.gone) + '\n';
    for (const auto& t : tags) s += "tag " + t.name + ' ' + t.hash + '\n';
    for (const auto& st : stashes) s += "stash " + st.ref + ' ' + st.hash + ' ' + st.message + '\n';
    for (const auto& r : remotes) s += "remote " + r.name + ' ' + r.fetchURL + '\n';
    for (const auto& m : submodules) s += "module " + m.path + ' ' + m.hash + '\n';
    return s;
}
void Sidebar::SetModel(const SidebarModel& model) {
    // A periodic refresh with nothing new must not rebuild (and re-select) anything.
    std::string signature = model.Signature();
    if (signature == fSignature && FullListCountItems() > 0) return;
    fSignature = signature;
    // Remember selection and collapsed sections across rebuilds.
    SidebarEntry previous; bool hadSelection = false;
    if (const SidebarEntry* selected = Selected()) { previous = *selected; hadSelection = true; }
    // Record every section's and remote's current state before the items go away.
    for (int32 i = 0; i < FullListCountItems(); ++i) {
        auto item = static_cast<SidebarItem*>(FullListItemAt(i));
        std::string key;
        if (item->Entry().kind == SidebarEntry::kSection) key = item->Entry().label;
        else if (item->Entry().kind == SidebarEntry::kRemote) key = "remote:" + item->Entry().name;
        else continue;
        fCollapsed.erase(std::remove(fCollapsed.begin(), fCollapsed.end(), key), fCollapsed.end());
        fExpanded.erase(std::remove(fExpanded.begin(), fExpanded.end(), key), fExpanded.end());
        (item->IsExpanded() ? fExpanded : fCollapsed).push_back(key);
    }
    while (FullListCountItems() > 0) delete RemoveItem(static_cast<int32>(0));
    std::vector<SidebarEntry> top;
    SidebarEntry wc; wc.kind = SidebarEntry::kWorkingCopy; wc.label = "Working Copy"; wc.count = model.changes;
    if (model.state.inProgress()) wc.detail = model.state.description();
    SidebarEntry all; all.kind = SidebarEntry::kHistoryAll; all.label = "History (all branches)";
    SidebarEntry current; current.kind = SidebarEntry::kHistoryCurrent; current.label = "History (" + (model.state.head.empty() ? "HEAD" : model.state.head) + ")";
    AddSection("Repository", SidebarEntry::kSection, {wc, all, current});
    std::vector<SidebarEntry> branches, tags, stashes, remotes, submodules;
    for (const auto& b : model.branches) {
        if (b.remote) continue;
        SidebarEntry e; e.kind = SidebarEntry::kBranch; e.label = b.name; e.name = b.name; e.hash = b.hash; e.current = b.current;
        e.ahead = b.ahead; e.behind = b.behind; e.gone = b.gone; e.detail = b.upstream;
        branches.push_back(e);
    }
    AddSection("Branches", SidebarEntry::kSection, branches);
    for (const auto& r : model.remotes) {
        SidebarEntry e; e.kind = SidebarEntry::kRemote; e.label = r.name; e.name = r.name; e.detail = r.fetchURL;
        remotes.push_back(e);
    }
    AddSection("Remotes", SidebarEntry::kSection, remotes);
    for (const auto& r : model.remotes) {
        SidebarItem* remoteItem = nullptr;
        for (int32 i = 0; i < FullListCountItems(); ++i) {
            auto item = static_cast<SidebarItem*>(FullListItemAt(i));
            if (item->Entry().kind == SidebarEntry::kRemote && item->Entry().name == r.name) remoteItem = item;
        }
        if (!remoteItem) continue;
        int count = 0;
        for (auto b = model.branches.rbegin(); b != model.branches.rend(); ++b) {
            if (!b->remote || b->remoteName() != r.name) continue;
            SidebarEntry e; e.kind = SidebarEntry::kRemoteBranch; e.label = b->shortName(); e.name = b->name; e.hash = b->hash;
            AddUnder(new SidebarItem(e, 2), remoteItem);
            ++count;
        }
        // Keep the user's choice; a remote with a long branch list starts collapsed.
        std::string key = "remote:" + r.name;
        bool expanded = std::find(fExpanded.begin(), fExpanded.end(), key) != fExpanded.end()
            || (std::find(fCollapsed.begin(), fCollapsed.end(), key) == fCollapsed.end() && count <= 30);
        if (expanded) Expand(remoteItem); else Collapse(remoteItem);
    }
    for (const auto& t : model.tags) { SidebarEntry e; e.kind = SidebarEntry::kTag; e.label = t.name; e.name = t.name; e.hash = t.hash; e.detail = t.subject; tags.push_back(e); }
    AddSection("Tags", SidebarEntry::kSection, tags);
    for (const auto& s : model.stashes) { SidebarEntry e; e.kind = SidebarEntry::kStash; e.label = s.message; e.name = s.ref; e.hash = s.hash; e.count = s.index; stashes.push_back(e); }
    AddSection("Stashes", SidebarEntry::kSection, stashes);
    if (model.hasGitHub) {
        SidebarEntry pr; pr.kind = SidebarEntry::kPullRequests; pr.label = "Pull Requests"; pr.count = model.pullRequests > 0 ? model.pullRequests : 0;
        AddSection("GitHub", SidebarEntry::kSection, {pr});
    }
    for (const auto& m : model.submodules) { SidebarEntry e; e.kind = SidebarEntry::kSubmodule; e.label = m.path; e.name = m.path; e.hash = m.hash; e.detail = m.describe; submodules.push_back(e); }
    AddSection("Submodules", SidebarEntry::kSection, submodules, false);
    // Restore collapse state of sections.
    for (int32 i = 0; i < FullListCountItems(); ++i) {
        auto item = static_cast<SidebarItem*>(FullListItemAt(i));
        if (item->Entry().kind == SidebarEntry::kSection && std::find(fCollapsed.begin(), fCollapsed.end(), item->Entry().label) != fCollapsed.end()) Collapse(item);
    }
    if (hadSelection) SelectKind(previous.kind, previous.name);
    else SelectKind(SidebarEntry::kWorkingCopy);
}
const SidebarEntry* Sidebar::EntryAt(int32 index) const {
    auto item = dynamic_cast<SidebarItem*>(ItemAt(index));
    return item ? &item->Entry() : nullptr;
}
const SidebarEntry* Sidebar::Selected() const { return EntryAt(CurrentSelection()); }
void Sidebar::SelectKind(SidebarEntry::Kind kind, const std::string& name) {
    for (int32 i = 0; i < CountItems(); ++i) {
        const SidebarEntry* entry = EntryAt(i);
        if (entry && entry->kind == kind && (name.empty() || entry->name == name)) { Select(i); ScrollToSelection(); return; }
    }
    if (!name.empty()) SelectKind(kind);   // fall back to the first entry of that kind
}
bool Sidebar::InitiateDrag(BPoint point, int32 index, bool wasSelected) {
    const SidebarEntry* entry = EntryAt(index);
    if (!entry) return false;
    uint32 what = 0;
    switch (entry->kind) {
        case SidebarEntry::kBranch: case SidebarEntry::kRemoteBranch: what = kDragBranch; break;
        case SidebarEntry::kTag: what = kDragTag; break;
        case SidebarEntry::kStash: what = kDragStash; break;
        default: return false;
    }
    BMessage drag(what);
    drag.AddString("name", entry->name.c_str());
    drag.AddString("hash", entry->hash.c_str());
    drag.AddBool("remote", entry->kind == SidebarEntry::kRemoteBranch);
    drag.AddString("label", entry->label.c_str());
    BRect frame = ItemFrame(index);
    DragMessage(&drag, frame, this);
    return true;
}
void Sidebar::MouseDown(BPoint where) {
    int32 buttons = 0;
    if (Window() && Window()->CurrentMessage()) Window()->CurrentMessage()->FindInt32("buttons", &buttons);
    int32 index = IndexOf(where);
    const SidebarEntry* entry = EntryAt(index);
    if (entry && entry->kind == SidebarEntry::kSection) {
        auto item = static_cast<SidebarItem*>(ItemAt(index));
        if (item->IsExpanded()) Collapse(item); else Expand(item);
        return;
    }
    // Clicking a remote's disclosure arrow toggles it like a section; remember the choice.
    if (entry && entry->kind == SidebarEntry::kRemote && where.x < 6 + 14 + 14) {
        auto item = static_cast<SidebarItem*>(ItemAt(index));
        if (item->IsExpanded()) Collapse(item); else Expand(item);
        return;
    }
    if ((buttons & B_SECONDARY_MOUSE_BUTTON) && entry) {
        Select(index);
        BMessage context(kSidebarInvoke);
        context.AddInt32("index", index);
        context.AddBool("context", true);
        context.AddPoint("screen", ConvertToScreen(where));
        fTarget.SendMessage(&context);
        return;
    }
    BOutlineListView::MouseDown(where);
}
void Sidebar::MouseMoved(BPoint where, uint32 transit, const BMessage* drag) {
    if (drag) {
        int32 index = IndexOf(where);
        if (transit == B_EXITED_VIEW) index = -1;
        if (index != fDropIndex) { fDropIndex = index; Invalidate(); }
    } else if (fDropIndex >= 0) { fDropIndex = -1; Invalidate(); }
    BOutlineListView::MouseMoved(where, transit, drag);
}
void Sidebar::MouseUp(BPoint where) { BOutlineListView::MouseUp(where); }
void Sidebar::Draw(BRect updateRect) {
    BOutlineListView::Draw(updateRect);
    if (fDropIndex >= 0 && fDropIndex < CountItems()) {
        BRect frame = ItemFrame(fDropIndex);
        SetHighColor(ui_color(B_CONTROL_HIGHLIGHT_COLOR));
        SetPenSize(2); StrokeRect(frame.InsetByCopy(1, 1)); SetPenSize(1);
    }
}
void Sidebar::KeyDown(const char* bytes, int32 count) {
    if (count == 1 && (bytes[0] == B_DELETE || bytes[0] == B_BACKSPACE)) {
        BMessage remove(kSidebarInvoke); remove.AddInt32("index", CurrentSelection()); remove.AddBool("delete", true);
        fTarget.SendMessage(&remove);
        return;
    }
    BOutlineListView::KeyDown(bytes, count);
}
void Sidebar::MessageReceived(BMessage* message) {
    if (message->WasDropped()) {
        BPoint where = ConvertFromScreen(message->DropPoint());
        int32 index = IndexOf(where);
        fDropIndex = -1; Invalidate();
        const SidebarEntry* entry = EntryAt(index);
        BMessage drop(kSidebarDrop);
        drop.AddMessage("payload", message);
        drop.AddInt32("index", index);
        if (entry) { drop.AddInt32("kind", entry->kind); drop.AddString("name", entry->name.c_str()); drop.AddString("hash", entry->hash.c_str()); drop.AddString("label", entry->label.c_str()); }
        drop.AddPoint("screen", message->DropPoint());
        fTarget.SendMessage(&drop);
        return;
    }
    BOutlineListView::MessageReceived(message);
}
}
