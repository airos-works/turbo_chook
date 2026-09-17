#pragma once
#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <Messenger.h>
#include <String.h>
#include <Window.h>
#include <string>
#include <vector>
class BTextControl; class BTextView; class BPopUpMenu; class BMenuField; class BView;

namespace tc {
inline rgb_color Mix(rgb_color a, rgb_color b, float amount) {
    auto channel = [&](uint8 x, uint8 y) { return static_cast<uint8>(x + (y - x) * amount); };
    return {channel(a.red, b.red), channel(a.green, b.green), channel(a.blue, b.blue), 255};
}
inline bool DarkUI() { rgb_color c = ui_color(B_PANEL_BACKGROUND_COLOR); return (c.red * 299 + c.green * 587 + c.blue * 114) / 1000 < 128; }
// Diff and badge colours adapted to the current control look.
struct Palette {
    rgb_color added, removed, addedText, removedText, hunk, header, muted, badge, badgeText, current, remote, tag, stash, conflict;
    static Palette Current();
};
// Short human date: "2026-09-17 14:05" from an ISO 8601 stamp, or "3 days ago" style when relative is set.
std::string FormatDate(const std::string& iso, bool relative = false);
std::string Elide(const std::string& text, size_t max);
void ShowError(const char* title, const std::string& text);
bool Confirm(const char* title, const std::string& text, const char* action, bool destructive = true);
void OpenExternally(const std::string& target);   // URL or path through Haiku's "open"

// A small window that asks for one or two text values and posts kPromptDone
// with "text"/"text2" and the original template's fields to the target.
class PromptWindow : public BWindow {
public:
    PromptWindow(const char* title, const char* label, const std::string& initial, BMessenger target, BMessage reply,
        const char* secondLabel = nullptr, const std::string& secondInitial = {}, const char* action = "OK", bool multiline = false,
        const std::vector<std::string>& choices = {}, const char* choiceLabel = nullptr);
    void MessageReceived(BMessage* message) override;
private:
    BTextControl* fText = nullptr;
    BTextControl* fSecond = nullptr;
    BTextView* fMultiline = nullptr;
    BMenuField* fChoiceField = nullptr;
    BPopUpMenu* fChoices = nullptr;
    BMessenger fTarget;
    BMessage fReply;
};
// Popup menu helper: shows items at a screen point and returns the chosen command (0 when dismissed).
struct MenuChoice { std::string label; uint32 command; bool enabled = true; bool separatorBefore = false; };
uint32 PopupChoice(BView* view, BPoint screenPoint, const std::vector<MenuChoice>& choices);
}
