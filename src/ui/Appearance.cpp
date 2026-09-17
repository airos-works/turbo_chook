#include "ui/Appearance.h"
#include "core/Settings.h"
#include "ui/Widgets.h"
#include <Button.h>
#include <CheckBox.h>
#include <InterfaceDefs.h>
#include <ListView.h>
#include <MenuBar.h>
#include <MenuField.h>
#include <ScrollBar.h>
#include <StringView.h>
#include <TextControl.h>
#include <TextView.h>
#include <View.h>
#include <Window.h>
#include <algorithm>
#include <cstring>

namespace tc {
namespace {
rgb_color Hex(uint32_t value) { return {static_cast<uint8>(value >> 16), static_cast<uint8>(value >> 8), static_cast<uint8>(value), 255}; }
// Kiri's palettes: background, panel, toolbar, text, muted, border, accent, selection,
// line, comment, keyword, string, number, type, added, removed.
struct PaletteDefinition { const char *id, *name; bool dark; uint32_t colors[16]; };
const PaletteDefinition kPalettes[] = {
    {"obsidian", "Obsidian", true, {0x171b22, 0x202630, 0x282f3a, 0xdce3ed, 0x8c9bad, 0x343e4b, 0x78b7ff, 0x334e70, 0x202936, 0x8195a6, 0xc6a0f6, 0xa6da95, 0xf5a97f, 0x8bd5ca, 0xa6da95, 0xed8796}},
    {"daylight", "Daylight", false, {0xfafbfd, 0xf0f2f6, 0xe5e9f0, 0x273448, 0x68788d, 0xcfd6e0, 0x2567c6, 0xc8ddf8, 0xf0f4fa, 0x718096, 0x8047a8, 0x3d7a40, 0xa55028, 0x187f86, 0x24723b, 0xc1384c}},
    {"nord", "Nord", true, {0x2e3440, 0x343c49, 0x3b4252, 0xeceff4, 0x9caec6, 0x4c566a, 0x88c0d0, 0x4c566a, 0x353e4e, 0x899db7, 0xb48ead, 0xa3be8c, 0xd08770, 0x8fbcbb, 0xa3be8c, 0xbf616a}},
    {"midnight", "Midnight", true, {0x101827, 0x162135, 0x1c2a42, 0xdde7ff, 0x8fa2c2, 0x30415f, 0x91b4ff, 0x2f4269, 0x19253d, 0x889ab5, 0xc4a7ff, 0x9cdaa0, 0xffcc85, 0x75cfe1, 0x91ce9c, 0xff9bac}},
    {"forest", "Forest", true, {0x17231e, 0x1e2d26, 0x26392f, 0xe0ece3, 0x9bb1a2, 0x3d5145, 0xb4d594, 0x355546, 0x21362c, 0x93aa97, 0xceb0de, 0xc6d88c, 0xebba91, 0x8acdbf, 0xaad389, 0xefadac}},
    {"ember", "Ember", true, {0x241c19, 0x2f2420, 0x3a2c26, 0xefe0d3, 0xb5a092, 0x564136, 0xe9b47e, 0x67452e, 0x342820, 0xaf9b8a, 0xdcb2d5, 0xbed293, 0xedb497, 0x85c9c0, 0xbdd892, 0xf6a29c}},
    {"linen", "Linen", false, {0xfffaf0, 0xf5eddf, 0xeae0cf, 0x40362b, 0x7a6d5d, 0xd9cbb6, 0x986333, 0xe9d4ad, 0xf7eddc, 0x81705e, 0x864775, 0x527038, 0xa15131, 0x316d79, 0x417342, 0xa1393f}},
    {"glacier", "Glacier", false, {0xf5fbff, 0xe9f2f8, 0xdce8f1, 0x233d50, 0x5f7888, 0xbfd0dd, 0x2068a2, 0xc1def3, 0xe9f4fb, 0x617b8b, 0x6e4898, 0x267257, 0x9a5125, 0x216d82, 0x267141, 0xbf3a52}},
    {"rose", "Rose", false, {0xfff8fa, 0xf6edf1, 0xf0dfe7, 0x47323e, 0x856a79, 0xdec9d3, 0x985373, 0xebc7d9, 0xf9eaf0, 0x8b7080, 0x84499b, 0x427849, 0xa0572b, 0x3a7586, 0x327344, 0xc13e56}},
    {"meadow", "Meadow", false, {0xf7fcf5, 0xebf3e6, 0xdeecd9, 0x2f4231, 0x647c65, 0xc5d8bd, 0x477b35, 0xcbe3c1, 0xeaf5e5, 0x657c64, 0x7c519b, 0x37743e, 0xa25a2c, 0x256f81, 0x307739, 0xa93f4d}},
};
Theme FromPalette(const PaletteDefinition& p) {
    Theme t;
    t.id = p.id; t.name = p.name; t.dark = p.dark; t.system = false;
    const uint32_t* c = p.colors;
    t.background = Hex(c[0]); t.panel = Hex(c[1]); t.toolbar = Hex(c[2]); t.text = Hex(c[3]); t.muted = Hex(c[4]);
    t.border = Hex(c[5]); t.accent = Hex(c[6]); t.selection = Hex(c[7]); t.selectionText = t.text;
    rgb_color keyword = Hex(c[10]), string = Hex(c[11]), number = Hex(c[12]), type = Hex(c[13]), added = Hex(c[14]), removed = Hex(c[15]);
    t.addedText = added; t.removedText = removed;
    t.added = Mix(t.background, added, p.dark ? 0.22f : 0.18f);
    t.removed = Mix(t.background, removed, p.dark ? 0.24f : 0.18f);
    t.hunk = Mix(t.background, t.accent, p.dark ? 0.2f : 0.16f);
    t.badge = Mix(t.panel, t.accent, p.dark ? 0.35f : 0.3f);
    t.current = Mix(t.panel, added, p.dark ? 0.45f : 0.35f);
    t.remote = Mix(t.panel, number, p.dark ? 0.4f : 0.3f);
    t.tag = Mix(t.panel, string, p.dark ? 0.4f : 0.3f);
    t.stash = Mix(t.panel, keyword, p.dark ? 0.4f : 0.3f);
    t.conflict = Mix(t.panel, removed, p.dark ? 0.5f : 0.4f);
    t.link = Mix(t.accent, type, 0.2f);
    return t;
}
}
const std::vector<Theme>& Theme::Builtins() {
    static const std::vector<Theme> themes = [] {
        std::vector<Theme> result;
        result.push_back(System());
        for (const auto& p : kPalettes) result.push_back(FromPalette(p));
        return result;
    }();
    return themes;
}
Theme Theme::System() {
    Theme t;
    t.id = "system"; t.name = "System (Haiku Appearance)"; t.system = true;
    t.background = ui_color(B_LIST_BACKGROUND_COLOR);
    t.panel = ui_color(B_PANEL_BACKGROUND_COLOR);
    t.toolbar = t.panel;
    t.text = ui_color(B_LIST_ITEM_TEXT_COLOR);
    t.dark = (t.panel.red * 299 + t.panel.green * 587 + t.panel.blue * 114) / 1000 < 128;
    t.muted = Mix(t.text, t.background, 0.45f);
    t.border = ui_color(B_CONTROL_BORDER_COLOR);
    t.accent = ui_color(B_CONTROL_HIGHLIGHT_COLOR);
    t.selection = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
    t.selectionText = ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR);
    t.link = ui_color(B_LINK_TEXT_COLOR);
    bool dark = t.dark;
    t.added = dark ? rgb_color{34, 74, 46, 255} : rgb_color{223, 246, 228, 255};
    t.removed = dark ? rgb_color{86, 40, 44, 255} : rgb_color{252, 226, 228, 255};
    t.addedText = dark ? rgb_color{140, 220, 160, 255} : rgb_color{24, 110, 52, 255};
    t.removedText = dark ? rgb_color{240, 150, 150, 255} : rgb_color{170, 34, 44, 255};
    t.hunk = dark ? rgb_color{44, 56, 74, 255} : rgb_color{228, 236, 250, 255};
    t.badge = dark ? rgb_color{70, 90, 120, 255} : rgb_color{214, 226, 246, 255};
    t.current = dark ? rgb_color{56, 130, 90, 255} : rgb_color{190, 236, 200, 255};
    t.remote = dark ? rgb_color{110, 90, 60, 255} : rgb_color{248, 232, 200, 255};
    t.tag = dark ? rgb_color{120, 100, 40, 255} : rgb_color{250, 240, 190, 255};
    t.stash = dark ? rgb_color{90, 70, 120, 255} : rgb_color{232, 220, 250, 255};
    t.conflict = dark ? rgb_color{140, 60, 40, 255} : rgb_color{255, 214, 190, 255};
    return t;
}
Theme Theme::ById(const std::string& id) {
    for (const auto& t : Builtins()) if (t.id == id) return t.system ? System() : t;
    return System();
}
Appearance& Appearance::Current() { static Appearance instance; return instance; }
Appearance::Appearance() : fTheme(Theme::System()) { Rebuild(); }
void Appearance::Load(Settings& settings) {
    fFamily = settings.Get("fontFamily");
    fSize = static_cast<float>(settings.GetInt("fontSize", 0));
    fTheme = Theme::ById(settings.Get("theme", "system"));
    Rebuild();
}
void Appearance::Save(Settings& settings) const {
    settings.Set("fontFamily", fFamily);
    settings.SetInt("fontSize", static_cast<int>(fSize));
    settings.Set("theme", fTheme.id);
}
void Appearance::Set(const std::string& family, float size, const std::string& themeID) {
    fFamily = family; fSize = size; fTheme = Theme::ById(themeID); Rebuild();
}
void Appearance::Rebuild() {
    fPlain = *be_plain_font; fBold = *be_bold_font; fFixed = *be_fixed_font;
    if (fTheme.system) fTheme = Theme::System();
    if (!fFamily.empty()) {
        font_family family; font_style style;
        std::strncpy(family, fFamily.c_str(), sizeof(family) - 1); family[sizeof(family) - 1] = 0;
        // Keep the plain and bold styles of the chosen family when it has them.
        fPlain.GetFamilyAndStyle(nullptr, &style);
        if (fPlain.SetFamilyAndStyle(family, "Regular") != B_OK) fPlain.SetFamilyAndFace(family, B_REGULAR_FACE);
        if (fBold.SetFamilyAndStyle(family, "Bold") != B_OK) fBold.SetFamilyAndFace(family, B_BOLD_FACE);
    }
    float size = fSize > 0 ? fSize : be_plain_font->Size();
    fPlain.SetSize(size); fBold.SetSize(size); fFixed.SetSize(size);
}
void Appearance::Apply(BWindow* window) const {
    if (!window) return;
    // Windows keep their content in an explicit root view so the whole area is themed.
    for (int32 i = 0; i < window->CountChildren(); ++i) Apply(window->ChildAt(i));
    window->InvalidateLayout(true);
}
void Appearance::Apply(BView* view) const {
    if (!view) return;
    const Theme& t = fTheme;
    if (dynamic_cast<BMenuBar*>(view)) {
        // Menus keep Haiku's own menu palette; their font follows the system too.
        for (int32 i = 0; i < view->CountChildren(); ++i) Apply(view->ChildAt(i));
        return;
    }
    if (dynamic_cast<BScrollBar*>(view)) return;
    bool isText = dynamic_cast<BTextView*>(view) != nullptr;
    bool isList = dynamic_cast<BListView*>(view) != nullptr;
    if (t.system) {
        view->SetViewUIColor(isList || isText ? B_LIST_BACKGROUND_COLOR : B_PANEL_BACKGROUND_COLOR);
        view->SetLowUIColor(isList || isText ? B_LIST_BACKGROUND_COLOR : B_PANEL_BACKGROUND_COLOR);
        view->SetHighUIColor(B_PANEL_TEXT_COLOR);
    } else {
        rgb_color back = isList || isText ? t.background : t.panel;
        view->SetViewColor(back); view->SetLowColor(back); view->SetHighColor(t.text);
    }
    // Standard controls take the chosen font; custom views read Appearance directly.
    if (auto control = dynamic_cast<BControl*>(view)) {
        control->SetFont(&fPlain);
        if (auto textControl = dynamic_cast<BTextControl*>(control)) {
            textControl->TextView()->SetFontAndColor(&fPlain, B_FONT_ALL, t.system ? nullptr : &t.text);
            if (!t.system) { textControl->TextView()->SetViewColor(t.background); textControl->TextView()->SetLowColor(t.background); }
        }
    } else if (auto label = dynamic_cast<BStringView*>(view)) {
        BFont current; label->GetFont(&current);
        bool bold = (current.Face() & B_BOLD_FACE) != 0;
        label->SetFont(bold ? &fBold : &fPlain);
        if (!t.system) label->SetHighColor(t.text);
    } else if (isText) {
        auto text = static_cast<BTextView*>(view);
        if (text->IsEditable()) text->SetFontAndColor(&fPlain, B_FONT_ALL, t.system ? nullptr : &t.text);
    } else if (isList) {
        static_cast<BListView*>(view)->SetFont(&fPlain);
    } else if (auto field = dynamic_cast<BMenuField*>(view)) {
        field->SetFont(&fPlain);
    }
    for (int32 i = 0; i < view->CountChildren(); ++i) Apply(view->ChildAt(i));
    view->Invalidate();
}
}
