#pragma once
#include "core/GitExecutable.h"
#include "core/Settings.h"
#include <Application.h>
#include <FilePanel.h>
#include <Locker.h>
#include <Messenger.h>
#include <memory>
#include <string>
#include <vector>

namespace tc {
class Application : public BApplication {
public:
    explicit Application(const char* signature = "application/x-vnd.TurboChook", std::string settingsPath = {});
    ~Application() override;
    void ReadyToRun() override;
    bool QuitRequested() override;
    void ArgvReceived(int32 argc, char** argv) override;
    void RefsReceived(BMessage* message) override;
    void MessageReceived(BMessage* message) override;
    void AboutRequested() override;
    // Settings are shared by every window; lock around read-modify-write.
    Settings& Preferences() { return fSettings; }
    BLocker& SettingsLock() { return fSettingsLock; }
    std::string GitExecutable();          // the configured or best available git
    std::string GitHubToken();
    std::vector<GitInstallation> Installations(bool rescan = false);
    // Opens (or activates) a repository window for a folder inside a repository.
    void OpenRepository(const std::string& path);
    void ShowWelcome();
    void BroadcastPreferences();
private:
    void OpenPanel(uint32 what);
    Settings fSettings;
    BLocker fSettingsLock;
    std::vector<GitInstallation> fInstallations;
    bool fScanned = false;
    std::vector<BMessenger> fWindows;
    BMessenger fWelcome, fPreferences;
    std::unique_ptr<BFilePanel> fOpenPanel, fNewPanel;
    bool fOpenedSomething = false;
};
Application* App();
}
