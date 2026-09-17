#pragma once
#include "ui/Async.h"
#include <FilePanel.h>
#include <Window.h>
#include <memory>
class BTextControl; class BButton; class BStringView;
namespace tc {
class CloneWindow : public BWindow {
public:
    explicit CloneWindow(const std::string& url = {});
    void MessageReceived(BMessage* message) override;
    bool QuitRequested() override;
private:
    BTextControl* fURL;
    BTextControl* fDestination;
    BButton* fClone;
    BStringView* fStatus;
    std::unique_ptr<BFilePanel> fPanel;
    std::unique_ptr<AsyncQueue> fJobs;
    bool fBusy = false;
};
}
