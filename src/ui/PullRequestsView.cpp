#include "ui/PullRequestsView.h"
#include "ui/Messages.h"
#include "ui/Widgets.h"
#include "ui/MarkdownText.h"
#include <Button.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextView.h>
#include <Window.h>
#include <cmath>

namespace tc {
namespace {
class PullRequestItem : public BListItem {
public:
    explicit PullRequestItem(PullRequest request) : fRequest(std::move(request)) {}
    void Update(BView* owner, const BFont* font) override {
        BListItem::Update(owner, font);
        font_height fh; font->GetHeight(&fh);
        SetHeight((std::ceil(fh.ascent + fh.descent)) * 2 + 12);
    }
    void DrawItem(BView* owner, BRect frame, bool) override {
        Palette palette = Palette::Current();
        rgb_color base = IsSelected() ? Palette::Current().selection : Palette::Current().background;
        rgb_color text = IsSelected() ? Palette::Current().selectionText : Palette::Current().text;
        owner->SetHighColor(base); owner->FillRect(frame);
        font_height fh; PlainFont().GetHeight(&fh);
        float line = std::ceil(fh.ascent + fh.descent);
        std::string status = fRequest.statusLine();
        rgb_color fill = status == "merged" ? palette.stash : status == "closed" ? palette.removed : status == "draft" ? palette.badge : palette.current;
        BFont small(&PlainFont()); small.SetSize(std::max(8.0f, PlainFont().Size() * 0.8f));
        owner->SetFont(&small);
        float w = small.StringWidth(status.c_str()) + 10;
        BRect box(frame.left + 8, frame.top + 6, frame.left + 8 + w, frame.top + 6 + line);
        owner->SetHighColor(fill); owner->FillRoundRect(box, 4, 4);
        owner->SetHighColor(DarkUI() ? rgb_color{240, 240, 240, 255} : rgb_color{30, 30, 30, 255});
        owner->DrawString(status.c_str(), BPoint(box.left + 5, box.top + fh.ascent - 1));
        owner->SetFont(&BoldFont()); owner->SetHighColor(text);
        BString title(("#" + std::to_string(fRequest.number) + "  " + fRequest.title).c_str());
        BoldFont().TruncateString(&title, B_TRUNCATE_END, frame.Width() - w - 24);
        owner->DrawString(title.String(), BPoint(box.right + 8, frame.top + 6 + fh.ascent));
        owner->SetFont(&PlainFont()); owner->SetHighColor(Mix(text, base, 0.35f));
        std::string meta = fRequest.author + "  " + fRequest.headRef + " \xe2\x86\x92 " + fRequest.baseRef + "  updated " + FormatDate(fRequest.updatedAt, true);
        BString metaText(meta.c_str());
        PlainFont().TruncateString(&metaText, B_TRUNCATE_END, frame.Width() - 16);
        owner->DrawString(metaText.String(), BPoint(frame.left + 8, frame.top + 8 + line + fh.ascent));
    }
    const PullRequest& Request() const { return fRequest; }
private:
    PullRequest fRequest;
};
}
PullRequestsView::PullRequestsView(BMessenger window) : BView("pullRequests", B_WILL_DRAW), fWindow(window) {
    SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
    fList = new BListView("prList", B_SINGLE_SELECTION_LIST);
    fList->SetSelectionMessage(new BMessage(kPullRequestSelected));
    fList->SetInvocationMessage(new BMessage(kPullRequestOpenBrowser));
    fDetails = new BTextView("prDetails");
    fDetails->MakeEditable(false);
    fDetails->SetStylable(true);
    fDetails->SetWordWrap(true);
    fDetails->SetInsets(8, 8, 8, 8);
    fTitle = new BStringView("prTitle", "Select a pull request");
    fTitle->SetFont(&BoldFont());
    fStatus = new BStringView("prStatus", "");
    fStatus->SetTruncation(B_TRUNCATE_END);
    fStatus->SetExplicitMinSize(BSize(60, B_SIZE_UNSET));
    fStatus->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));
    fTitle->SetTruncation(B_TRUNCATE_END);
    fTitle->SetExplicitMinSize(BSize(60, B_SIZE_UNSET));
    fStateMenu = new BPopUpMenu("state");
    for (const char* state : {"Open", "Closed", "All"}) { BMessage* m = new BMessage(kPullRequestState); m->AddString("state", state); fStateMenu->AddItem(new BMenuItem(state, m)); }
    fStateMenu->ItemAt(0)->SetMarked(true);
    fStateField = new BMenuField("stateField", "Show:", fStateMenu);
    fCheckout = new BButton("prCheckout", "Checkout Branch", new BMessage(kPullRequestCheckout));
    fMerge = new BButton("prMerge", "Merge" B_UTF8_ELLIPSIS, new BMessage(kPullRequestMerge));
    fClose = new BButton("prClose", "Close", new BMessage(kPullRequestClose));
    fBrowser = new BButton("prBrowser", "Open on GitHub", new BMessage(kPullRequestOpenBrowser));
    fComment = new BButton("prComment", "Comment" B_UTF8_ELLIPSIS, new BMessage(kPullRequestComment));
    auto listScroll = new BScrollView("prListScroll", fList, 0, false, true);
    auto detailScroll = new BScrollView("prDetailScroll", fDetails, 0, false, true);
    BLayoutBuilder::Group<>(this, B_VERTICAL, 0).SetInsets(B_USE_SMALL_INSETS)
        .AddGroup(B_HORIZONTAL)
            .Add(fStateField).Add(new BButton("prRefresh", "Refresh", new BMessage(kPullRequestsRefresh)))
            .AddGlue().Add(fStatus).AddGlue()
            .Add(new BButton("prNew", "New Pull Request" B_UTF8_ELLIPSIS, new BMessage(kNewPullRequest)))
        .End()
        .AddSplit(B_HORIZONTAL, 0.0f)
            .Add(listScroll, 0.4f)
            .AddGroup(B_VERTICAL, B_USE_SMALL_SPACING, 0.6f)
                .Add(fTitle)
                .Add(detailScroll, 1.0f)
                .AddGroup(B_HORIZONTAL).Add(fCheckout).Add(fMerge).Add(fClose).Add(fComment).AddGlue().Add(fBrowser).End()
            .End()
        .End()
    .End();
}
void PullRequestsView::AttachedToWindow() {
    BView::AttachedToWindow();
    for (BButton* b : {fCheckout, fMerge, fClose, fBrowser, fComment}) b->SetTarget(this);
    for (const char* name : {"prRefresh", "prNew"}) if (auto b = dynamic_cast<BButton*>(FindView(name))) b->SetTarget(this);
    fStateMenu->SetTargetForItems(this);
    fList->SetTarget(this);
    UpdateButtons();
}
std::string PullRequestsView::State() const {
    BMenuItem* marked = fStateMenu->FindMarked();
    std::string state = marked ? marked->Label() : "Open";
    for (auto& c : state) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return state;
}
const PullRequest* PullRequestsView::Selected() const {
    auto item = dynamic_cast<PullRequestItem*>(fList->ItemAt(fList->CurrentSelection()));
    return item ? &item->Request() : nullptr;
}
void PullRequestsView::SetRepository(const std::string& repository, bool hasToken) {
    fRepository = repository; fHasToken = hasToken;
    if (repository.empty()) SetStatus("This repository has no GitHub remote.");
    else if (!hasToken) SetStatus(repository + " (read only: add a GitHub token in Preferences to create, merge or comment)");
    else SetStatus(repository);
    UpdateButtons();
}
void PullRequestsView::Restyle() {
    fList->SetFont(&PlainFont());
    for (int32 i = 0; i < fList->CountItems(); ++i) fList->ItemAt(i)->Update(fList, &PlainFont());
    fList->Invalidate();
    fDetails->SetViewColor(Palette::Current().background); fDetails->SetLowColor(Palette::Current().background);
    const PullRequest* pr = Selected();
    if (pr) { BMessage again(kPullRequestSelected); MessageReceived(&again); }
}
void PullRequestsView::SetStatus(const std::string& text) { fStatus->SetText(text.c_str()); }
void PullRequestsView::SetPullRequests(std::vector<PullRequest> requests, const std::string& error) {
    int selectedNumber = Selected() ? Selected()->number : 0;
    fRequests = std::move(requests);
    fList->MakeEmpty();
    for (const auto& pr : fRequests) fList->AddItem(new PullRequestItem(pr));
    if (!error.empty()) { fTitle->SetText("Pull requests unavailable"); fDetails->SetText(error.c_str()); }
    else if (fRequests.empty()) { fTitle->SetText("No pull requests"); fDetails->SetText(("There are no " + State() + " pull requests for " + fRepository + ".").c_str()); }
    for (int32 i = 0; i < fList->CountItems(); ++i) if (fRequests[i].number == selectedNumber) fList->Select(i);
    UpdateButtons();
}
void PullRequestsView::SetDetails(const PullRequest& pr, const std::vector<PullRequestComment>& comments, const std::vector<PullRequestReview>& reviews) {
    fTitle->SetText(("#" + std::to_string(pr.number) + "  " + pr.title).c_str());
    // The description, reviews and comments are GitHub Markdown; compose one document and render it.
    std::string text;
    text += "**" + pr.statusLine() + "** by **" + pr.author + "**  " + pr.headRepository + ":`" + pr.headRef + "` \xe2\x86\x92 `" + pr.baseRef + "`  \n";
    text += "Opened " + FormatDate(pr.createdAt) + ", updated " + FormatDate(pr.updatedAt);
    if (pr.commits || pr.changedFiles) text += " \xc2\xb7 " + std::to_string(pr.commits) + " commits, " + std::to_string(pr.changedFiles) + " files, +" + std::to_string(pr.additions) + " -" + std::to_string(pr.deletions);
    if (!pr.mergeable.empty()) text += " \xc2\xb7 " + pr.mergeable;
    text += "\n\n" + (pr.body.empty() ? std::string("*(no description)*") : pr.body) + "\n";
    if (!reviews.empty() || !comments.empty()) text += "\n---\n";
    for (const auto& review : reviews) {
        text += "\n**" + review.author + "** " + review.state + " \xc2\xb7 " + FormatDate(review.submittedAt) + "\n\n";
        if (!review.body.empty()) text += review.body + "\n";
    }
    for (const auto& comment : comments) {
        text += "\n**" + comment.author + "** \xc2\xb7 " + FormatDate(comment.createdAt);
        if (!comment.path.empty()) text += " \xc2\xb7 `" + comment.path + (comment.line ? ":" + std::to_string(comment.line) : "") + "`";
        text += "\n\n" + comment.body + "\n";
    }
    Palette palette = Palette::Current();
    MarkdownStyle style{Palette::Current().text, Palette::Current().link, palette.addedText, palette.muted, palette.muted, PlainFont().Size()};
    fLinks = SetMarkdown(fDetails, text, style);
    UpdateButtons();
}
void PullRequestsView::UpdateButtons() {
    const PullRequest* pr = Selected();
    bool open = pr && pr->state == "open";
    fCheckout->SetEnabled(pr != nullptr);
    fMerge->SetEnabled(open && fHasToken && !pr->draft);
    fClose->SetEnabled(pr && fHasToken && !pr->merged);
    fClose->SetLabel(pr && pr->state == "closed" ? "Reopen" : "Close");
    fBrowser->SetEnabled(pr != nullptr);
    fComment->SetEnabled(pr != nullptr && fHasToken);
    if (auto b = dynamic_cast<BButton*>(FindView("prNew"))) b->SetEnabled(fHasToken && !fRepository.empty());
}
void PullRequestsView::MessageReceived(BMessage* message) {
    switch (message->what) {
        case kPullRequestSelected: {
            UpdateButtons();
            const PullRequest* pr = Selected();
            if (!pr) break;
            fTitle->SetText(("#" + std::to_string(pr->number) + "  " + pr->title).c_str());
            Palette palette = Palette::Current();
            MarkdownStyle style{Palette::Current().text, Palette::Current().link, palette.addedText, palette.muted, palette.muted, PlainFont().Size()};
            fLinks = SetMarkdown(fDetails, "*Loading details" B_UTF8_ELLIPSIS "*\n\n" + pr->body, style);
            BMessage forward(kPullRequestSelected); forward.AddInt32("number", pr->number);
            fWindow.SendMessage(&forward);
            break;
        }
        case kPullRequestState: case kPullRequestsRefresh: fWindow.SendMessage(kPullRequestsRefresh); break;
        case kPullRequestCheckout: case kPullRequestMerge: case kPullRequestClose: case kPullRequestOpenBrowser: case kPullRequestComment: {
            const PullRequest* pr = Selected();
            if (!pr) break;
            BMessage forward(message->what); forward.AddInt32("number", pr->number);
            fWindow.SendMessage(&forward);
            break;
        }
        case kNewPullRequest: fWindow.SendMessage(kNewPullRequest); break;
        default: BView::MessageReceived(message);
    }
}
}
