#pragma once
#include "core/GitHub.h"
#include "ui/Widgets.h"
#include "ui/MarkdownText.h"
#include <ListView.h>
#include <Messenger.h>
#include <View.h>
#include <vector>
class BTextView; class BButton; class BStringView; class BMenuField; class BPopUpMenu;
namespace tc {
// GitHub pull requests for the repository's origin: list, details, checkout,
// merge, close and comments. Network work is done by the window's queue.
class PullRequestsView : public BView {
public:
    explicit PullRequestsView(BMessenger window);
    void AttachedToWindow() override;
    void MessageReceived(BMessage* message) override;
    void SetPullRequests(std::vector<PullRequest> requests, const std::string& error);
    void SetDetails(const PullRequest& request, const std::vector<PullRequestComment>& comments, const std::vector<PullRequestReview>& reviews);
    void SetStatus(const std::string& text);
    void Restyle();
    void SetRepository(const std::string& repository, bool hasToken);
    std::string State() const;
    const PullRequest* Selected() const;
    const std::vector<PullRequest>& Requests() const { return fRequests; }
private:
    void UpdateButtons();
    BMessenger fWindow;
    BListView* fList;
    BTextView* fDetails;
    BStringView* fTitle;
    BStringView* fStatus;
    BPopUpMenu* fStateMenu;
    BMenuField* fStateField;
    BButton* fCheckout;
    BButton* fMerge;
    BButton* fClose;
    BButton* fBrowser;
    BButton* fComment;
    std::vector<PullRequest> fRequests;
    std::vector<MarkdownLink> fLinks;
    std::string fRepository;
    bool fHasToken = false;
};
}
