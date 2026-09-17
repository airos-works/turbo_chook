#include "ui/PreferencesWindow.h"
#include "ui/Application.h"
#include "ui/Messages.h"
#include "ui/Widgets.h"
#include "core/Git.h"
#include "ui/Appearance.h"
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
#include <TabView.h>
#include <GroupView.h>
#include <algorithm>
#include <cstring>

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
    // Look & Feel: font family, size and theme for every Turbo Chook window.
    const Appearance& appearance = Appearance::Current();
    fFontMenu = new BPopUpMenu("font");
    BMenuItem* systemFont = new BMenuItem("System font", new BMessage(kMenuAction));
    fFontMenu->AddItem(systemFont);
    fFontMenu->AddSeparatorItem();
    bool markedFont = false;
    for (int32 i = 0; i < count_font_families(); ++i) {
        font_family family;
        if (get_font_family(i, &family) != B_OK) continue;
        BMessage* message = new BMessage(kMenuAction); message->AddString("family", family);
        auto item = new BMenuItem(family, message);
        fFontMenu->AddItem(item);
        if (appearance.FontFamily() == family) { item->SetMarked(true); markedFont = true; }
    }
    if (!markedFont) systemFont->SetMarked(true);
    fFontField = new BMenuField("fontField", "Font:", fFontMenu);
    fFontSize = new BTextControl("fontSize", "Size (points):", appearance.FontSize() > 0 ? std::to_string(static_cast<int>(appearance.FontSize())).c_str() : "", new BMessage(kMenuAction));
    fFontSize->SetModificationMessage(new BMessage(kMenuAction));
    fThemeMenu = new BPopUpMenu("theme");
    for (const auto& theme : Theme::Builtins()) {
        BMessage* message = new BMessage(kMenuAction); message->AddString("theme", theme.id.c_str());
        auto item = new BMenuItem((theme.name + (theme.system ? "" : theme.dark ? "  (dark)" : "  (light)")).c_str(), message);
        fThemeMenu->AddItem(item);
        if (theme.id == appearance.ThemeID()) item->SetMarked(true);
    }
    if (!fThemeMenu->FindMarked()) fThemeMenu->ItemAt(0)->SetMarked(true);
    fThemeField = new BMenuField("themeField", "Theme:", fThemeMenu);
    fPreview = new BStringView("preview", "Preview: main \xe2\x86\x92 origin/main  \xe2\x86\x91 1  \xe2\x80\xa2 Merge feature/parser into main");
    fPreview->SetExplicitMinSize(BSize(300, 48));
    fPreview->SetAlignment(B_ALIGN_CENTER);
    auto lookBox = new BBox("lookBox"); lookBox->SetLabel("Fonts and colours");
    BLayoutBuilder::Group<>(lookBox, B_VERTICAL, B_USE_SMALL_SPACING).SetInsets(B_USE_DEFAULT_SPACING)
        .AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
            .Add(fFontField->CreateLabelLayoutItem(), 0, 0).Add(fFontField->CreateMenuBarLayoutItem(), 1, 0)
            .Add(fFontSize->CreateLabelLayoutItem(), 0, 1).Add(fFontSize->CreateTextViewLayoutItem(), 1, 1)
            .Add(fThemeField->CreateLabelLayoutItem(), 0, 2).Add(fThemeField->CreateMenuBarLayoutItem(), 1, 2)
        .End()
        .Add(fPreview)
        .Add(new BStringView("lookHint", "The theme colours every list, diff and history view; System follows Haiku's Appearance preferences. Leave the size empty for the system size."))
    .End();
    auto general = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
    general->SetName("General");
    BLayoutBuilder::Group<>(general).SetInsets(B_USE_DEFAULT_SPACING).Add(gitBox).Add(githubBox).Add(authorBox).Add(workflowBox).AddGlue().End();
    auto look = new BGroupView(B_VERTICAL, B_USE_DEFAULT_SPACING);
    look->SetName("Look & Feel");
    BLayoutBuilder::Group<>(look).SetInsets(B_USE_DEFAULT_SPACING).Add(lookBox).AddGlue().End();
    auto tabs = new BTabView("tabs", B_WIDTH_FROM_LABEL);
    tabs->AddTab(general);
    tabs->AddTab(look);
    tabs->SetBorder(B_NO_BORDER);
    auto apply = new BButton("apply", "Apply", new BMessage(kApplyPreferences));
    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING).SetInsets(B_USE_WINDOW_INSETS)
        .Add(tabs)
        .AddGroup(B_HORIZONTAL).AddGlue().Add(new BButton("close", "Close", new BMessage(B_QUIT_REQUESTED))).Add(apply).End()
    .End();
    UpdatePreview();
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
        BMenuItem* font = fFontMenu->FindMarked();
        const char* family = nullptr;
        if (font && font->Message()) font->Message()->FindString("family", &family);
        settings.Set("fontFamily", family ? family : "");
        int size = std::atoi(fFontSize->Text());
        settings.SetInt("fontSize", size >= 8 && size <= 32 ? size : 0);
        BMenuItem* theme = fThemeMenu->FindMarked();
        const char* themeID = nullptr;
        if (theme && theme->Message()) theme->Message()->FindString("theme", &themeID);
        settings.Set("theme", themeID ? themeID : "system");
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
void PreferencesWindow::UpdatePreview() {
    BMenuItem* font = fFontMenu->FindMarked();
    const char* family = nullptr;
    if (font && font->Message()) font->Message()->FindString("family", &family);
    BMenuItem* theme = fThemeMenu->FindMarked();
    const char* themeID = nullptr;
    if (theme && theme->Message()) theme->Message()->FindString("theme", &themeID);
    Theme colors = Theme::ById(themeID ? themeID : "system");
    BFont preview(be_plain_font);
    if (family) { font_family f; std::strncpy(f, family, sizeof(f) - 1); f[sizeof(f) - 1] = 0; preview.SetFamilyAndFace(f, B_REGULAR_FACE); }
    int size = std::atoi(fFontSize->Text());
    if (size >= 8 && size <= 32) preview.SetSize(size);
    fPreview->SetFont(&preview);
    fPreview->SetViewColor(colors.background); fPreview->SetLowColor(colors.background); fPreview->SetHighColor(colors.text);
    fPreview->Invalidate();
}
void PreferencesWindow::MessageReceived(BMessage* message) {
    switch (message->what) {
        case kApplyPreferences: Apply(); break;
        case kMenuAction: UpdatePreview(); break;
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
