#pragma once
#include <Window.h>
#include <string>
#include <vector>
class BListView; class BButton;
namespace tc {
class WelcomeWindow : public BWindow {
public:
    WelcomeWindow();
    void MessageReceived(BMessage* message) override;
    bool QuitRequested() override;
private:
    void Reload();
    std::string SelectedPath() const;
    BListView* fRecent;
    BButton* fOpenButton;
    std::vector<std::string> fPaths;
};
}
