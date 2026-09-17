#pragma once
#include <Font.h>
#include <GraphicsDefs.h>
#include <string>
#include <vector>
class BView; class BWindow;
namespace tc {
class Settings;
// A colour theme for the whole interface. "system" follows Haiku's Appearance
// preferences; the others are the palettes Kiri ships, adapted to a Git client.
struct Theme {
    std::string id, name;
    bool dark = false, system = true;
    rgb_color background, panel, toolbar, text, muted, border, accent, selection, selectionText,
        added, removed, addedText, removedText, hunk, badge, current, remote, tag, stash, conflict, link;
    static const std::vector<Theme>& Builtins();
    static Theme System();                    // built from ui_color() each call
    static Theme ById(const std::string& id);
};
// Fonts and theme shared by every window. Loaded from settings by the
// application; windows call Apply() when preferences change.
class Appearance {
public:
    static Appearance& Current();
    void Load(Settings& settings);
    void Save(Settings& settings) const;
    const Theme& Colors() const { return fTheme; }
    const BFont& Plain() const { return fPlain; }
    const BFont& Bold() const { return fBold; }
    const BFont& Fixed() const { return fFixed; }
    float FontSize() const { return fSize; }
    const std::string& FontFamily() const { return fFamily; }
    const std::string& ThemeID() const { return fTheme.id; }
    void Set(const std::string& family, float size, const std::string& themeID);
    // Recursively applies colours and fonts to standard controls in a window.
    void Apply(BWindow* window) const;
    void Apply(BView* view) const;
private:
    Appearance();
    void Rebuild();
    Theme fTheme;
    std::string fFamily;
    float fSize = 0;
    BFont fPlain, fBold, fFixed;
};
}
