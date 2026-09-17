#include "ui/Application.h"
#include "ui/Messages.h"
#include "ui/RepositoryWindow.h"
#include "ui/WelcomeWindow.h"
#include "ui/PreferencesWindow.h"
#include "ui/CloneWindow.h"
#include "ui/Appearance.h"
#include "core/Git.h"
#include <Alert.h>
#include <Autolock.h>
#include <Entry.h>
#include <Path.h>
#include <Roster.h>
#include <algorithm>
#include <sys/stat.h>

namespace tc {
Application* App() { return static_cast<Application*>(be_app); }
Application::Application(const char* signature, std::string settingsPath)
    : BApplication(signature), fSettings(settingsPath.empty() ? Settings::DefaultPath() : settingsPath), fSettingsLock("settings") {
    fSettings.Load();
    Appearance::Current().Load(fSettings);
}
Application::~Application() {}
void Application::ReadyToRun() {
    if (!fOpenedSomething) ShowWelcome();
}
bool Application::QuitRequested() {
    for (int32 i = 0; i < CountWindows(); ++i) {
        BWindow* window = WindowAt(i);
        if (window == nullptr) continue;
        if (dynamic_cast<RepositoryWindow*>(window) == nullptr) continue;
        BMessenger messenger(window);
        BMessage reply;
        messenger.SendMessage(B_QUIT_REQUESTED, &reply);
    }
    return BApplication::QuitRequested();
}
void Application::ArgvReceived(int32 argc, char** argv) {
    for (int32 i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') continue;
        BPath path(argv[i]);
        if (path.InitCheck() == B_OK) { OpenRepository(path.Path()); fOpenedSomething = true; }
    }
}
void Application::RefsReceived(BMessage* message) {
    entry_ref ref;
    for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; ++i) {
        BPath path(&ref);
        if (path.InitCheck() != B_OK) continue;
        struct stat st;
        std::string directory = path.Path();
        if (stat(directory.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) {
            BPath parent; path.GetParent(&parent); directory = parent.Path();
        }
        OpenRepository(directory);
        fOpenedSomething = true;
    }
}
void Application::MessageReceived(BMessage* message) {
    switch (message->what) {
        case kOpenRepository: OpenPanel(kRepositoryChosen); break;
        case kNewRepository: OpenPanel(kNewRepositoryChosen); break;
        case kRepositoryChosen: {
            entry_ref ref;
            if (message->FindRef("refs", &ref) == B_OK) { BPath path(&ref); OpenRepository(path.Path()); }
            break;
        }
        case kNewRepositoryChosen: {
            entry_ref ref;
            if (message->FindRef("refs", &ref) != B_OK) break;
            BPath path(&ref);
            GitOptions options; options.executable = GitExecutable();
            auto result = GitRepository::Init(path.Path(), options);
            if (!result.ok()) { (new BAlert("New Repository", result.diagnostic().c_str(), "OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go(); break; }
            OpenRepository(path.Path());
            break;
        }
        case kCloneRepository: (new CloneWindow())->Show(); break;
        case kShowWelcome: ShowWelcome(); break;
        case kShowPreferences: {
            BWindow* existing = nullptr;
            for (int32 i = 0; i < CountWindows(); ++i) if ((existing = dynamic_cast<PreferencesWindow*>(WindowAt(i))) != nullptr) break;
            if (existing) existing->Activate(); else { auto window = new PreferencesWindow(); fPreferences = BMessenger(window); window->Show(); }
            break;
        }
        case kPreferencesChanged: BroadcastPreferences(); break;
        case kRepositoryClosed: {
            BMessenger sender = message->ReturnAddress();
            fWindows.erase(std::remove_if(fWindows.begin(), fWindows.end(), [](const BMessenger& m) { return !m.IsValid(); }), fWindows.end());
            // Show the welcome window again when the last repository closes so the app stays reachable.
            bool anyRepository = false;
            for (int32 i = 0; i < CountWindows(); ++i) if (dynamic_cast<RepositoryWindow*>(WindowAt(i)) != nullptr && BMessenger(WindowAt(i)) != sender) anyRepository = true;
            if (!anyRepository) ShowWelcome();
            break;
        }
        case B_SILENT_RELAUNCH: ShowWelcome(); break;
        default: BApplication::MessageReceived(message);
    }
}
void Application::AboutRequested() {
    BAlert* about = new BAlert("About Turbo Chook",
        "Turbo Chook 0.1.1 alpha\n\nA native Git client for Haiku: branches, history graph, diffs, stashes, "
        "pull requests, undo and drag & drop workflows.\n\nNamed after the Tasmanian nativehen, the turbo chook.\n\n"
        "\xc2\xa9 2026 Turbo Chook contributors. MIT license.", "OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_INFO_ALERT);
    about->Go(nullptr);
}
void Application::OpenPanel(uint32 what) {
    auto& panel = what == kRepositoryChosen ? fOpenPanel : fNewPanel;
    if (!panel) {
        BMessage message(what);
        panel.reset(new BFilePanel(B_OPEN_PANEL, new BMessenger(this), nullptr, B_DIRECTORY_NODE, false, &message));
        panel->Window()->SetTitle(what == kRepositoryChosen ? "Open Repository" : "Choose Folder for New Repository");
        panel->SetButtonLabel(B_DEFAULT_BUTTON, what == kRepositoryChosen ? "Open" : "Create");
    }
    panel->Show();
}
std::string Application::GitExecutable() {
    std::string configured;
    { BAutolock lock(fSettingsLock); configured = fSettings.Get("gitExecutable"); }
    if (!configured.empty()) {
        struct stat st;
        if (stat(configured.c_str(), &st) == 0) return configured;
    }
    auto installations = Installations();
    if (installations.empty()) return "git";
    // Prefer the newest usable version when nothing is configured.
    auto best = std::max_element(installations.begin(), installations.end(), [](const GitInstallation& a, const GitInstallation& b) {
        return CompareGitVersions(a.version, b.version) < 0; });
    return best->path;
}
std::string Application::GitHubToken() { BAutolock lock(fSettingsLock); return fSettings.Get("gitHubToken"); }
std::vector<GitInstallation> Application::Installations(bool rescan) {
    BAutolock lock(fSettingsLock);
    if (!fScanned || rescan) {
        fInstallations = DiscoverGitInstallations();
        for (const auto& custom : fSettings.List("customGitExecutables")) {
            bool known = std::any_of(fInstallations.begin(), fInstallations.end(), [&](const GitInstallation& i) { return i.path == custom; });
            if (!known) { auto probe = ProbeGitExecutable(custom, "custom"); if (probe.usable()) fInstallations.push_back(probe); }
        }
        fScanned = true;
    }
    return fInstallations;
}
void Application::OpenRepository(const std::string& path) {
    GitOptions options; options.executable = GitExecutable();
    GitRepository probe(path, options);
    std::string root = probe.Root();
    if (root.empty()) {
        BAlert* alert = new BAlert("Open Repository", ("\"" + path + "\" is not inside a Git repository.\n\nCreate a new repository there?").c_str(),
            "Cancel", "Create Repository", nullptr, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
        if (alert->Go() == 1) {
            auto result = GitRepository::Init(path, options);
            if (!result.ok()) { (new BAlert("New Repository", result.diagnostic().c_str(), "OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT))->Go(); return; }
            root = path;
        } else return;
    }
    for (int32 i = 0; i < CountWindows(); ++i) {
        auto window = dynamic_cast<RepositoryWindow*>(WindowAt(i));
        if (window && window->Root() == root) { window->Activate(); return; }
    }
    { BAutolock lock(fSettingsLock); fSettings.TouchRepository(root); fSettings.Save(); }
    auto window = new RepositoryWindow(root);
    fWindows.push_back(BMessenger(window));
    window->Show();
    if (fWelcome.IsValid()) fWelcome.SendMessage(kRefresh);
    // Close the welcome window once a repository is open.
    for (int32 i = 0; i < CountWindows(); ++i) {
        auto welcome = dynamic_cast<WelcomeWindow*>(WindowAt(i));
        if (welcome) welcome->PostMessage(B_QUIT_REQUESTED);
    }
}
void Application::ShowWelcome() {
    for (int32 i = 0; i < CountWindows(); ++i) {
        auto welcome = dynamic_cast<WelcomeWindow*>(WindowAt(i));
        if (welcome) { welcome->Activate(); return; }
    }
    auto window = new WelcomeWindow();
    fWelcome = BMessenger(window);
    window->Show();
}
void Application::BroadcastPreferences() {
    Installations(true);
    { BAutolock lock(fSettingsLock); Appearance::Current().Load(fSettings); }
    for (int32 i = 0; i < CountWindows(); ++i) {
        BWindow* window = WindowAt(i);
        if (dynamic_cast<PreferencesWindow*>(window) == nullptr) window->PostMessage(kPreferencesChanged);
    }
}
}
