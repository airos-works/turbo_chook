#pragma once
#include "core/GitExecutable.h"
#include <FilePanel.h>
#include <Window.h>
#include <memory>
#include <vector>
class BMenuField; class BPopUpMenu; class BTextControl; class BStringView; class BCheckBox;
namespace tc {
// Git installation choice, GitHub access, author identity and workflow defaults.
class PreferencesWindow : public BWindow {
public:
    PreferencesWindow();
    void MessageReceived(BMessage* message) override;
private:
    void PopulateGit(const std::string& selected);
    void Apply();
    void UpdatePreview();
    BPopUpMenu* fGitMenu;
    BMenuField* fGitField;
    BStringView* fGitInfo;
    BTextControl* fToken;
    BTextControl* fApiBase;
    BTextControl* fName;
    BTextControl* fEmail;
    BCheckBox* fPrune;
    BCheckBox* fPullRebase;
    BCheckBox* fShowAll;
    BTextControl* fContext;
    BPopUpMenu* fFontMenu;
    BMenuField* fFontField;
    BTextControl* fFontSize;
    BPopUpMenu* fThemeMenu;
    BMenuField* fThemeField;
    BStringView* fPreview;
    std::vector<GitInstallation> fInstallations;
    std::unique_ptr<BFilePanel> fPanel;
};
}
