#include "ui/CloneWindow.h"
#include "ui/Application.h"
#include "ui/Messages.h"
#include "ui/Widgets.h"
#include "core/Git.h"
#include <Button.h>
#include <Entry.h>
#include <LayoutBuilder.h>
#include <Path.h>
#include <StringView.h>
#include <TextControl.h>
#include <cstdlib>

namespace tc {
CloneWindow::CloneWindow(const std::string& url)
    : BWindow(BRect(0, 0, 520, 160), "Clone Repository", B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE) {
    fJobs.reset(new AsyncQueue(BMessenger(this)));
    fURL = new BTextControl("url", "Repository URL:", url.c_str(), nullptr);
    const char* home = getenv("HOME");
    fDestination = new BTextControl("destination", "Clone into folder:", (std::string(home ? home : "/boot/home") + "/").c_str(), nullptr);
    fClone = new BButton("clone", "Clone", new BMessage(kCloneStart));
    fStatus = new BStringView("status", "");
    BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
        .SetInsets(B_USE_WINDOW_INSETS)
        .AddGrid(B_USE_DEFAULT_SPACING, B_USE_SMALL_SPACING)
            .Add(fURL->CreateLabelLayoutItem(), 0, 0).Add(fURL->CreateTextViewLayoutItem(), 1, 0, 2)
            .Add(fDestination->CreateLabelLayoutItem(), 0, 1).Add(fDestination->CreateTextViewLayoutItem(), 1, 1)
            .Add(new BButton("browse", "Browse" B_UTF8_ELLIPSIS, new BMessage(kBrowseFolder)), 2, 1)
        .End()
        .Add(fStatus)
        .AddGroup(B_HORIZONTAL).AddGlue().Add(new BButton("cancel", "Cancel", new BMessage(B_QUIT_REQUESTED))).Add(fClone).End()
    .End();
    fClone->MakeDefault(true);
    fURL->MakeFocus(true);
    CenterOnScreen();
}
void CloneWindow::MessageReceived(BMessage* message) {
    switch (message->what) {
        case kWorkDone: fJobs->Drain(); break;
        case kBrowseFolder: {
            if (!fPanel) { BMessage chosen(kFolderChosen); fPanel.reset(new BFilePanel(B_OPEN_PANEL, new BMessenger(this), nullptr, B_DIRECTORY_NODE, false, &chosen)); fPanel->SetButtonLabel(B_DEFAULT_BUTTON, "Choose"); }
            fPanel->Show();
            break;
        }
        case kFolderChosen: {
            entry_ref ref;
            if (message->FindRef("refs", &ref) == B_OK) {
                BPath path(&ref);
                std::string url = fURL->Text();
                auto slash = url.find_last_of('/');
                std::string name = slash == std::string::npos ? "" : url.substr(slash + 1);
                if (name.size() > 4 && name.substr(name.size() - 4) == ".git") name.resize(name.size() - 4);
                fDestination->SetText((std::string(path.Path()) + "/" + name).c_str());
            }
            break;
        }
        case kCloneStart: {
            if (fBusy) break;
            std::string url = Trim(fURL->Text()), destination = Trim(fDestination->Text());
            if (url.empty()) { fStatus->SetText("Enter a repository URL."); break; }
            if (destination.empty() || destination.back() == '/') {
                auto slash = url.find_last_of('/');
                std::string name = slash == std::string::npos ? url : url.substr(slash + 1);
                auto colon = name.find_last_of(':'); if (colon != std::string::npos) name = name.substr(colon + 1);
                if (name.size() > 4 && name.substr(name.size() - 4) == ".git") name.resize(name.size() - 4);
                if (name.empty()) { fStatus->SetText("Choose a destination folder."); break; }
                destination += name;
                fDestination->SetText(destination.c_str());
            }
            fBusy = true; fClone->SetEnabled(false);
            fStatus->SetText(("Cloning " + url + B_UTF8_ELLIPSIS).c_str());
            GitOptions options; options.executable = App()->GitExecutable(); options.gitHubToken = App()->GitHubToken();
            fJobs->Submit([url, destination, options, this](const std::atomic<bool>& cancel) -> AsyncQueue::Callback {
                auto result = GitRepository::Clone(url, destination, options, &cancel);
                return [result, destination, window = this] {   // callbacks run in this window's thread
                    window->fBusy = false; window->fClone->SetEnabled(true);
                    if (result.ok()) { App()->OpenRepository(destination); window->PostMessage(B_QUIT_REQUESTED); }
                    else { window->fStatus->SetText("Clone failed."); ShowError("Clone Repository", Trim(result.diagnostic())); }
                };
            });
            break;
        }
        default: BWindow::MessageReceived(message);
    }
}
bool CloneWindow::QuitRequested() {
    if (fBusy && !Confirm("Clone Repository", "A clone is still running. Cancel it?", "Cancel Clone")) return false;
    fJobs->StopAndWait();
    return true;
}
}
