#include "ui/PreferencesWindow.h"
#include "ui/Application.h"
#include "ui/Messages.h"
#include "ui/Widgets.h"
#include "core/Git.h"
#include <Autolock.h>
#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <StringView.h>
#include <TextControl.h>
#include <algorithm>

namespace tc {
PreferencesWindow::PreferencesWindow()
    : BWindow(BRect(0, 0, 560, 420), "Turbo Chook Preferences", B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE) {
    Settings& settings = App()->Preferences();
    std::string executable, token, apiBase;
    bool prune, pullRebase, showAll; int context;
    {
        BAutolock lock(App()->SettingsLock());
        executable = settings.Get("gitExecutable"); token = settings.Get("gitHubToken");
        apiBase = settings.Get("gitHubApi", "https://api.github.com");
        prune = settings.GetBool("fetchPrune", true); pullRebase = settings.GetBool("pullRebase", false);
        showAll = settings.GetBool("showAllBranches", true); context = settings.GetInt("diffContext", 3);
    }
    fGitMenu = new BPopUpMenu("git");
    fGitField = new BMenuField("gitField", "Git installation:", fGitMenu);
    fGitInfo = new BStringView("gitInfo", "");
    fGitInfo->SetFont(be_plain_font);
    fToken = new BTextControl("token", "Personal access token:", token.c_str(), nullptr);
    fToken->TextView()->HideTyping(true);
    fApiBase = new BTextControl("api", "API base URL:", apiBase.c_str(), nullptr);
    GitOptions options; options.executable = App()->GitExecutable();
    GitRepository global("/boot/home", options);
    auto name = global.Config("user.name", true), email = global.Config("user.email", true);
    fName = new BTextControl("name", "Name:", name.ok() ? Trim(name.output).c_str() : "", nullptr);
    fEmail = new BTextControl("email", "Email:", email.ok() ? Trim(email.output).c_str() : "", nullptr);
    fPrune = new BCheckBox("prune", "Prune deleted remote branches when fetching", nullptr);
    fPrune->SetValue(prune ? B_CONTROL_ON : B_CONTROL_OFF);
    fPullRebase = new BCheckBox("pullRebase", "Pull with rebase instead of merge", nullptr);
    fPullRebase->SetValue(pullRebase ? B_CONTROL_ON : B_CONTROL_OFF);
    fShowAll = new BCheckBox("showAll", "History shows all branches by default", nullptr);
    fShowAll->SetValue(showAll ? B_CONTROL_ON : B_CONTROL_OFF);
    fContext = new BTextControl("context", "Diff context lines:", std::to_string(context).c_str(), nullptr);
    auto gitBox = new BBox("gitBox"); gitBox->SetLabel("Git");
    BLayoutBuilder::Group<>(gitBox, B_VERTICAL, B_USE_SMALL_SPACING).SetInsets(B_USE_DEFAULT_SPACING)
        .AddGroup(B_HORIZONTAL).Add(fGitField).Add(new BButton("rescan", "Rescan", new BMessage(kRescanGit))).Add(new BButton("browseGit", "Other" B_UTF8_ELLIPSIS, new BMessage(kBrowseGit))).End()
        .Add(fGitInfo)
        .Add(new BStringView("gitHint", "Every packaged or non-packaged Git on this system is listed; repositories use the selected version."))
    .End();
    auto githubBox = new BBox("githubBox"); githubBox->SetLabel("GitHub");
    BLayoutBuilder::Group<>(githubBox, B_VERTICAL, B_USE_SMALL_SPACING).SetInsets(B_USE_DEFAULT_SPACING)
        .AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
            .Add(fToken->CreateLabelLayoutItem(), 0, 0).Add(fToken->CreateTextViewLayoutItem(), 1, 0)
            .Add(fApiBase->CreateLabelLayoutItem(), 0, 1).Add(fApiBase->CreateTextViewLayoutItem(), 1, 1)
        .End()
        .Add(new BStringView("tokenHint", "The token lists, creates and merges pull requests and authenticates HTTPS pushes to github.com."))
    .End();
    auto authorBox = new BBox("authorBox"); authorBox->SetLabel("Commit author (global Git configuration)");
    BLayoutBuilder::Group<>(authorBox, B_VERTICAL, B_USE_SMALL_SPACING).SetInsets(B_USE_DEFAULT_SPACING)
        .AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
            .Add(fName->CreateLabelLayoutItem(), 0, 0).Add(fName->CreateTextViewLayoutItem(), 1, 0)
            .Add(fEmail->CreateLabelLayoutItem(), 0, 1).Add(fEmail->CreateTextViewLayoutItem(), 1, 1)
        .End()
    .End();
    auto workflowBox = new BBox("workflowBox"); workflowBox->SetLabel("Workflow");
    BLayoutBuilder::Group<>(workflowBox, B_VERTICAL, B_USE_SMALL_SPACING).SetInsets(B_USE_DEFAULT_SPACING)
        .Add(fPrune).Add(fPullRebase).Add(fShowAll).Add(fContext)
    .End();
    auto apply = new BButton("apply", "Apply", new BMessage(kApplyPreferences));
    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING).SetInsets(B_USE_WINDOW_INSETS)
        .Add(gitBox).Add(githubBox).Add(authorBox).Add(workflowBox)
        .AddGroup(B_HORIZONTAL).AddGlue().Add(new BButton("close", "Close", new BMessage(B_QUIT_REQUESTED))).Add(apply).End()
    .End();
    apply->MakeDefault(true);
    PopulateGit(executable);
    CenterOnScreen();
}
void PreferencesWindow::PopulateGit(const std::string& selected) {
    while (fGitMenu->CountItems() > 0) delete fGitMenu->RemoveItem(static_cast<int32>(0));
    fInstallations = App()->Installations(true);
    std::sort(fInstallations.begin(), fInstallations.end(), [](const GitInstallation& a, const GitInstallation& b) { return CompareGitVersions(a.version, b.version) > 0; });
    BMenuItem* automatic = new BMenuItem("Newest available (automatic)", new BMessage(kSelectGit));
    fGitMenu->AddItem(automatic);
    bool marked = false;
    for (size_t i = 0; i < fInstallations.size(); ++i) {
        BMessage* message = new BMessage(kSelectGit); message->AddInt32("index", static_cast<int32>(i));
        auto item = new BMenuItem(fInstallations[i].label().c_str(), message);
        fGitMenu->AddItem(item);
        if (fInstallations[i].path == selected) { item->SetMarked(true); marked = true; }
    }
    if (!marked) automatic->SetMarked(true);
    if (fInstallations.empty()) fGitInfo->SetText("No Git installation was found. Install git with pkgman or choose an executable.");
    else {
        std::string active = App()->GitExecutable();
        auto found = std::find_if(fInstallations.begin(), fInstallations.end(), [&](const GitInstallation& i) { return i.path == active; });
        fGitInfo->SetText(found != fInstallations.end() ? ("Active: Git " + found->version + " at " + found->path).c_str() : ("Active: " + active).c_str());
    }
}
void PreferencesWindow::Apply() {
    Settings& settings = App()->Preferences();
    {
        BAutolock lock(App()->SettingsLock());
        BMenuItem* marked = fGitMenu->FindMarked();
        int32 index = -1;
        if (marked && marked->Message()) marked->Message()->FindInt32("index", &index);
        settings.Set("gitExecutable", index >= 0 && index < static_cast<int32>(fInstallations.size()) ? fInstallations[index].path : "");
        settings.Set("gitHubToken", Trim(fToken->Text()));
        settings.Set("gitHubApi", Trim(fApiBase->Text()).empty() ? "https://api.github.com" : Trim(fApiBase->Text()));
        settings.SetBool("fetchPrune", fPrune->Value() == B_CONTROL_ON);
        settings.SetBool("pullRebase", fPullRebase->Value() == B_CONTROL_ON);
        settings.SetBool("showAllBranches", fShowAll->Value() == B_CONTROL_ON);
        int context = std::atoi(fContext->Text());
        settings.SetInt("diffContext", context < 0 ? 0 : context > 50 ? 50 : context);
        settings.Save();
    }
    GitOptions options; options.executable = App()->GitExecutable();
    GitRepository global("/boot/home", options);
    std::string name = Trim(fName->Text()), email = Trim(fEmail->Text());
    if (!name.empty()) global.SetConfig("user.name", name, true);
    if (!email.empty()) global.SetConfig("user.email", email, true);
    be_app->PostMessage(kPreferencesChanged);
    PopulateGit(App()->Preferences().Get("gitExecutable"));
}
void PreferencesWindow::MessageReceived(BMessage* message) {
    switch (message->what) {
        case kApplyPreferences: Apply(); break;
        case kRescanGit: PopulateGit(App()->Preferences().Get("gitExecutable")); break;
        case kSelectGit: break;
        case kBrowseGit: {
            if (!fPanel) { BMessage chosen(kGitChosen); fPanel.reset(new BFilePanel(B_OPEN_PANEL, new BMessenger(this), nullptr, B_FILE_NODE, false, &chosen)); fPanel->SetButtonLabel(B_DEFAULT_BUTTON, "Use"); }
            fPanel->Show();
            break;
        }
        case kGitChosen: {
            entry_ref ref;
            if (message->FindRef("refs", &ref) != B_OK) break;
            BPath path(&ref);
            auto probe = ProbeGitExecutable(path.Path(), "custom");
            if (!probe.usable()) { ShowError("Git Installation", std::string(path.Path()) + " did not answer to \"git --version\"."); break; }
            {
                BAutolock lock(App()->SettingsLock());
                auto list = App()->Preferences().List("customGitExecutables");
                if (std::find(list.begin(), list.end(), probe.path) == list.end()) list.push_back(probe.path);
                App()->Preferences().SetList("customGitExecutables", list);
                App()->Preferences().Set("gitExecutable", probe.path);
                App()->Preferences().Save();
            }
            PopulateGit(probe.path);
            be_app->PostMessage(kPreferencesChanged);
            break;
        }
        default: BWindow::MessageReceived(message);
    }
}
}
