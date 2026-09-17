#include "ui/Widgets.h"
#include "ui/Messages.h"
#include "core/Process.h"
#include <Alert.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <ScrollView.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <View.h>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace tc {
Palette Palette::Current() {
    Palette p;
    bool dark = DarkUI();
    rgb_color base = ui_color(B_LIST_BACKGROUND_COLOR);
    p.added = dark ? rgb_color{34, 74, 46, 255} : rgb_color{223, 246, 228, 255};
    p.removed = dark ? rgb_color{86, 40, 44, 255} : rgb_color{252, 226, 228, 255};
    p.addedText = dark ? rgb_color{140, 220, 160, 255} : rgb_color{24, 110, 52, 255};
    p.removedText = dark ? rgb_color{240, 150, 150, 255} : rgb_color{170, 34, 44, 255};
    p.hunk = dark ? rgb_color{44, 56, 74, 255} : rgb_color{228, 236, 250, 255};
    p.header = Mix(base, ui_color(B_PANEL_BACKGROUND_COLOR), 0.7f);
    p.muted = Mix(ui_color(B_LIST_ITEM_TEXT_COLOR), base, 0.45f);
    p.badge = dark ? rgb_color{70, 90, 120, 255} : rgb_color{214, 226, 246, 255};
    p.badgeText = dark ? rgb_color{200, 220, 250, 255} : rgb_color{30, 60, 120, 255};
    p.current = dark ? rgb_color{56, 130, 90, 255} : rgb_color{190, 236, 200, 255};
    p.remote = dark ? rgb_color{110, 90, 60, 255} : rgb_color{248, 232, 200, 255};
    p.tag = dark ? rgb_color{120, 100, 40, 255} : rgb_color{250, 240, 190, 255};
    p.stash = dark ? rgb_color{90, 70, 120, 255} : rgb_color{232, 220, 250, 255};
    p.conflict = dark ? rgb_color{140, 60, 40, 255} : rgb_color{255, 214, 190, 255};
    return p;
}
std::string FormatDate(const std::string& iso, bool relative) {
    if (iso.size() < 16) return iso;
    if (!relative) return iso.substr(0, 10) + " " + iso.substr(11, 5);
    struct tm tm{};
    std::memset(&tm, 0, sizeof(tm));
    int year, month, day, hour, minute, second = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) < 5) return iso.substr(0, 10);
    tm.tm_year = year - 1900; tm.tm_mon = month - 1; tm.tm_mday = day; tm.tm_hour = hour; tm.tm_min = minute; tm.tm_sec = second;
    // timegm is not available on Haiku: convert as local time, then remove the local offset.
    time_t stamp = mktime(&tm);
    struct tm local{}; localtime_r(&stamp, &local);
    stamp += local.tm_gmtoff;
    // Apply the ISO offset so relative times are right regardless of zone.
    auto plus = iso.find_last_of("+-");
    if (plus != std::string::npos && plus > 10) {
        int oh = 0, om = 0;
        if (std::sscanf(iso.c_str() + plus + 1, "%d:%d", &oh, &om) >= 1) stamp -= (iso[plus] == '+' ? 1 : -1) * (oh * 3600 + om * 60);
    }
    time_t now = time(nullptr);
    long delta = static_cast<long>(now - stamp);
    if (delta < 0) delta = 0;
    if (delta < 60) return "just now";
    if (delta < 3600) return std::to_string(delta / 60) + (delta / 60 == 1 ? " minute ago" : " minutes ago");
    if (delta < 86400) return std::to_string(delta / 3600) + (delta / 3600 == 1 ? " hour ago" : " hours ago");
    if (delta < 86400 * 30) return std::to_string(delta / 86400) + (delta / 86400 == 1 ? " day ago" : " days ago");
    if (delta < 86400 * 365) return std::to_string(delta / (86400 * 30)) + (delta / (86400 * 30) == 1 ? " month ago" : " months ago");
    return std::to_string(delta / (86400 * 365)) + (delta / (86400 * 365) == 1 ? " year ago" : " years ago");
}
std::string Elide(const std::string& text, size_t max) {
    if (text.size() <= max) return text;
    return text.substr(0, max > 3 ? max - 3 : 0) + "...";
}
void ShowError(const char* title, const std::string& text) {
    BAlert* alert = new BAlert(title, text.empty() ? "The operation failed." : text.c_str(), "OK", nullptr, nullptr, B_WIDTH_AS_USUAL, B_STOP_ALERT);
    alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
    alert->Go(nullptr);
}
bool Confirm(const char* title, const std::string& text, const char* action, bool destructive) {
    BAlert* alert = new BAlert(title, text.c_str(), "Cancel", action, nullptr, B_WIDTH_AS_USUAL, destructive ? B_WARNING_ALERT : B_INFO_ALERT);
    alert->SetShortcut(0, B_ESCAPE);
    return alert->Go() == 1;
}
void OpenExternally(const std::string& target) {
    ProcessOptions options; options.timeout = std::chrono::milliseconds(10000);
    RunProcess({"open", target}, options);
}
PromptWindow::PromptWindow(const char* title, const char* label, const std::string& initial, BMessenger target, BMessage reply,
    const char* secondLabel, const std::string& secondInitial, const char* action, bool multiline,
    const std::vector<std::string>& choices, const char* choiceLabel)
    : BWindow(BRect(0, 0, 420, 120), title, B_FLOATING_WINDOW_LOOK, B_MODAL_APP_WINDOW_FEEL,
        B_AUTO_UPDATE_SIZE_LIMITS | B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE | B_NOT_MINIMIZABLE | B_CLOSE_ON_ESCAPE),
      fTarget(target), fReply(reply) {
    auto ok = new BButton("ok", action, new BMessage(kPromptDone));
    auto cancel = new BButton("cancel", "Cancel", new BMessage(kPromptCancel));
    auto builder = BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING).SetInsets(B_USE_WINDOW_INSETS);
    if (!choices.empty()) {
        fChoices = new BPopUpMenu("choices");
        for (const auto& choice : choices) fChoices->AddItem(new BMenuItem(choice.c_str(), nullptr));
        if (fChoices->CountItems() > 0) fChoices->ItemAt(0)->SetMarked(true);
        fChoiceField = new BMenuField("choiceField", choiceLabel ? choiceLabel : "", fChoices);
        builder.Add(fChoiceField);
    }
    if (multiline) {
        // A single-line field for the label plus a multi-line body for the second label.
        if (secondLabel) { fText = new BTextControl("text", label, initial.c_str(), nullptr); builder.Add(fText); }
        fMultiline = new BTextView("body");
        fMultiline->SetText(secondLabel ? secondInitial.c_str() : initial.c_str());
        fMultiline->SetWordWrap(true);
        fMultiline->SetExplicitMinSize(BSize(420, 140));
        builder.Add(new BStringView("bodyLabel", secondLabel ? secondLabel : label)).Add(new BScrollView("scroll", fMultiline, 0, false, true));
    } else {
        fText = new BTextControl("text", label, initial.c_str(), nullptr);
        builder.Add(fText);
        if (secondLabel) { fSecond = new BTextControl("second", secondLabel, secondInitial.c_str(), nullptr); builder.Add(fSecond); }
    }
    builder.AddGroup(B_HORIZONTAL).AddGlue().Add(cancel).Add(ok).End().End();
    ok->MakeDefault(true);
    if (fText) { fText->MakeFocus(true); fText->TextView()->SelectAll(); }
    else if (fMultiline) fMultiline->MakeFocus(true);
    CenterOnScreen();
}
void PromptWindow::MessageReceived(BMessage* message) {
    switch (message->what) {
        case kPromptDone: {
            BMessage reply(fReply);
            if (fText) reply.AddString("text", fText->Text());
            if (fMultiline) reply.AddString(fText ? "text2" : "text", fMultiline->Text());
            if (fSecond) reply.AddString("text2", fSecond->Text());
            if (fChoices) { BMenuItem* marked = fChoices->FindMarked(); reply.AddInt32("choice", marked ? fChoices->IndexOf(marked) : 0); }
            fTarget.SendMessage(&reply);
            PostMessage(B_QUIT_REQUESTED);
            break;
        }
        case kPromptCancel: PostMessage(B_QUIT_REQUESTED); break;
        default: BWindow::MessageReceived(message);
    }
}
uint32 PopupChoice(BView* view, BPoint screenPoint, const std::vector<MenuChoice>& choices) {
    BPopUpMenu* menu = new BPopUpMenu("popup", false, false);
    for (const auto& choice : choices) {
        if (choice.separatorBefore) menu->AddSeparatorItem();
        auto item = new BMenuItem(choice.label.c_str(), new BMessage(choice.command));
        item->SetEnabled(choice.enabled);
        menu->AddItem(item);
    }
    BMenuItem* chosen = menu->Go(screenPoint, false, true, BRect(screenPoint.x - 2, screenPoint.y - 2, screenPoint.x + 2, screenPoint.y + 2), false);
    uint32 command = chosen && chosen->Message() ? chosen->Message()->what : 0;
    delete menu;
    return command;
}
}
