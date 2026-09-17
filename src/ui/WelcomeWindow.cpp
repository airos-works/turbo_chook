#include "ui/WelcomeWindow.h"
#include "ui/Application.h"
#include "ui/Messages.h"
#include "ui/Widgets.h"
#include <Autolock.h>
#include <Button.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <ListView.h>
#include <Path.h>
#include <ScrollView.h>
#include <StringView.h>
#include <sys/stat.h>

namespace tc {
namespace {
class RecentItem : public BListItem {
public:
    RecentItem(std::string name, std::string path, bool exists) : fName(std::move(name)), fPath(std::move(path)), fExists(exists) {}
    void DrawItem(BView* owner, BRect frame, bool complete) override {
        rgb_color base = IsSelected() ? ui_color(B_LIST_SELECTED_BACKGROUND_COLOR) : ui_color(B_LIST_BACKGROUND_COLOR);
        owner->SetHighColor(base); owner->FillRect(frame);
        rgb_color text = IsSelected() ? ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR) : ui_color(B_LIST_ITEM_TEXT_COLOR);
        if (!fExists) text = Mix(text, base, 0.5f);
        BFont bold(be_bold_font); font_height fh; bold.GetHeight(&fh);
        owner->SetFont(&bold); owner->SetHighColor(text);
        owner->DrawString(fName.c_str(), BPoint(frame.left + 8, frame.top + fh.ascent + 4));
        BFont plain(be_plain_font); plain.SetSize(plain.Size() * 0.9f); owner->SetFont(&plain);
        owner->SetHighColor(Mix(text, base, 0.35f));
        owner->DrawString((fExists ? fPath : fPath + "  (missing)").c_str(), BPoint(frame.left + 8, frame.top + fh.ascent + fh.descent + fh.ascent + 6));
        owner->SetFont(be_plain_font);
    }
    void Update(BView* owner, const BFont* font) override {
        BListItem::Update(owner, font);
        font_height fh; be_bold_font->GetHeight(&fh);
        SetHeight((fh.ascent + fh.descent) * 2 + 12);
    }
    const std::string& Path() const { return fPath; }
private:
    std::string fName, fPath; bool fExists;
};
}
WelcomeWindow::WelcomeWindow()
    : BWindow(BRect(120, 120, 640, 560), "Turbo Chook", B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE) {
    fRecent = new BListView("recent", B_SINGLE_SELECTION_LIST);
    fRecent->SetSelectionMessage(new BMessage(kSidebarSelection));
    fRecent->SetInvocationMessage(new BMessage(kOpenRecent));
    auto scroll = new BScrollView("recentScroll", fRecent, 0, false, true);
    fOpenButton = new BButton("openRecent", "Open", new BMessage(kOpenRecent));
    fOpenButton->SetEnabled(false);
    auto title = new BStringView("title", "Turbo Chook");
    BFont big(be_bold_font); big.SetSize(big.Size() * 1.6f); title->SetFont(&big);
    auto subtitle = new BStringView("subtitle", "Git management for Haiku. Open a repository, clone one, or start fresh.");
    auto forget = new BButton("forget", "Remove from List", new BMessage(kForgetRecent));
    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
        .SetInsets(B_USE_WINDOW_INSETS)
        .Add(title).Add(subtitle)
        .AddGroup(B_HORIZONTAL)
            .Add(new BButton("open", "Open Repository" B_UTF8_ELLIPSIS, new BMessage(kOpenRepository)))
            .Add(new BButton("clone", "Clone Repository" B_UTF8_ELLIPSIS, new BMessage(kCloneRepository)))
            .Add(new BButton("new", "New Repository" B_UTF8_ELLIPSIS, new BMessage(kNewRepository)))
            .AddGlue()
            .Add(new BButton("prefs", "Preferences" B_UTF8_ELLIPSIS, new BMessage(kShowPreferences)))
        .End()
        .Add(new BStringView("recentLabel", "Recent repositories"))
        .Add(scroll, 1.0f)
        .AddGroup(B_HORIZONTAL).Add(forget).AddGlue().Add(fOpenButton).End()
    .End();
    fOpenButton->MakeDefault(true);
    Reload();
    CenterOnScreen();
}
void WelcomeWindow::Reload() {
    fRecent->MakeEmpty();
    fPaths.clear();
    Application* app = App();
    BAutolock lock(app->SettingsLock());
    for (const auto& path : app->Preferences().Repositories()) {
        struct stat st;
        bool exists = stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        auto slash = path.rfind('/');
        fRecent->AddItem(new RecentItem(slash == std::string::npos ? path : path.substr(slash + 1), path, exists));
        fPaths.push_back(path);
    }
}
std::string WelcomeWindow::SelectedPath() const {
    int32 index = fRecent->CurrentSelection();
    return index >= 0 && index < static_cast<int32>(fPaths.size()) ? fPaths[index] : "";
}
void WelcomeWindow::MessageReceived(BMessage* message) {
    switch (message->what) {
        case kSidebarSelection: fOpenButton->SetEnabled(fRecent->CurrentSelection() >= 0); break;
        case kOpenRecent: { auto path = SelectedPath(); if (!path.empty()) App()->OpenRepository(path); break; }
        case kForgetRecent: {
            auto path = SelectedPath();
            if (path.empty()) break;
            { BAutolock lock(App()->SettingsLock()); App()->Preferences().ForgetRepository(path); App()->Preferences().Save(); }
            Reload();
            break;
        }
        case kRefresh: Reload(); break;
        case kOpenRepository: case kCloneRepository: case kNewRepository: case kShowPreferences:
            be_app->PostMessage(message); break;
        case B_SIMPLE_DATA: {
            // A folder dragged from Tracker opens that repository.
            entry_ref ref;
            if (message->FindRef("refs", &ref) == B_OK) { BPath path(&ref); App()->OpenRepository(path.Path()); }
            break;
        }
        default: BWindow::MessageReceived(message);
    }
}
bool WelcomeWindow::QuitRequested() {
    bool repositoryOpen = false;
    for (int32 i = 0; i < be_app->CountWindows(); ++i) {
        BWindow* window = be_app->WindowAt(i);
        if (window != this && window->Title() != nullptr && dynamic_cast<WelcomeWindow*>(window) == nullptr
            && std::string(window->Title()).find("Preferences") == std::string::npos && std::string(window->Title()).find("Clone") == std::string::npos)
            repositoryOpen = true;
    }
    if (!repositoryOpen) be_app->PostMessage(B_QUIT_REQUESTED);
    return true;
}
}
