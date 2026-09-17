#include "ui/MarkdownText.h"
#include "md4c/md4c.h"
#include <Font.h>
#include <TextView.h>
#include <vector>

namespace tc {
namespace {
// Builds the text and a list of style runs while md4c walks the document.
struct Builder {
    std::string text;
    struct Run { int32 start; int32 end; unsigned style; };
    enum { kBold = 1, kItalic = 2, kCode = 4, kHeading = 8, kQuote = 16, kLink = 32, kStrike = 64, kCodeBlock = 128 };
    std::vector<Run> runs;
    std::vector<MarkdownLink> links;
    unsigned style = 0;
    int headingLevel = 0;
    int quoteDepth = 0;
    std::vector<int> listNumbers;   // per nesting level: 0 for bullets, else the next number
    std::string linkTarget; int32 linkStart = 0;
    bool atLineStart = true;
    void Append(const std::string& piece) {
        if (piece.empty()) return;
        int32 start = static_cast<int32>(text.size());
        text += piece;
        runs.push_back({start, static_cast<int32>(text.size()), style | (headingLevel ? kHeading : 0) | (quoteDepth ? kQuote : 0)});
        atLineStart = !piece.empty() && piece.back() == '\n';
    }
    void NewLine() { if (!atLineStart) Append("\n"); }
    void BlankLine() { NewLine(); if (text.size() < 2 || text[text.size() - 2] != '\n') Append("\n"); }
    void Indent() {
        std::string prefix(quoteDepth, '|');
        if (!prefix.empty()) prefix = "\xe2\x94\x82 ";   // "│ "
        if (listNumbers.size() > 1) prefix += std::string((listNumbers.size() - 1) * 4, ' ');
        Append(prefix);
    }
};
int EnterBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto& b = *static_cast<Builder*>(userdata);
    switch (type) {
        case MD_BLOCK_DOC: break;
        case MD_BLOCK_QUOTE: b.BlankLine(); ++b.quoteDepth; break;
        case MD_BLOCK_UL: if (b.listNumbers.empty()) b.BlankLine(); else b.NewLine(); b.listNumbers.push_back(0); break;
        case MD_BLOCK_OL: { if (b.listNumbers.empty()) b.BlankLine(); else b.NewLine(); auto* d = static_cast<MD_BLOCK_OL_DETAIL*>(detail); b.listNumbers.push_back(static_cast<int>(d->start)); break; }
        case MD_BLOCK_LI: {
            b.NewLine(); b.Indent();
            auto* d = static_cast<MD_BLOCK_LI_DETAIL*>(detail);
            if (b.listNumbers.empty()) b.Append("\xe2\x80\xa2 ");
            else if (b.listNumbers.back() == 0) b.Append(d->is_task ? (d->task_mark == ' ' ? "\xe2\x98\x90 " : "\xe2\x98\x91 ") : "\xe2\x80\xa2 ");
            else { b.Append(std::to_string(b.listNumbers.back()) + ". "); ++b.listNumbers.back(); }
            break;
        }
        case MD_BLOCK_HR: b.BlankLine(); b.Append("\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\n"); break;
        case MD_BLOCK_H: { b.BlankLine(); b.headingLevel = static_cast<int>(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level); b.Indent(); break; }
        case MD_BLOCK_CODE: b.BlankLine(); b.style |= Builder::kCodeBlock | Builder::kCode; break;
        case MD_BLOCK_HTML: b.BlankLine(); break;
        case MD_BLOCK_P: if (b.listNumbers.empty() || !b.atLineStart) { if (b.listNumbers.empty()) b.BlankLine(); } if (b.listNumbers.empty()) b.Indent(); break;
        case MD_BLOCK_TABLE: b.BlankLine(); break;
        case MD_BLOCK_THEAD: case MD_BLOCK_TBODY: break;
        case MD_BLOCK_TR: b.NewLine(); break;
        case MD_BLOCK_TH: b.style |= Builder::kBold; break;
        case MD_BLOCK_TD: break;
        default: break;
    }
    return 0;
}
int LeaveBlock(MD_BLOCKTYPE type, void*, void* userdata) {
    auto& b = *static_cast<Builder*>(userdata);
    switch (type) {
        case MD_BLOCK_QUOTE: --b.quoteDepth; b.NewLine(); break;
        case MD_BLOCK_UL: case MD_BLOCK_OL: b.listNumbers.pop_back(); b.NewLine(); break;
        case MD_BLOCK_LI: b.NewLine(); break;
        case MD_BLOCK_H: b.headingLevel = 0; b.NewLine(); break;
        case MD_BLOCK_CODE: b.style &= ~(Builder::kCodeBlock | Builder::kCode); b.NewLine(); break;
        case MD_BLOCK_P: b.NewLine(); break;
        case MD_BLOCK_TH: b.style &= ~Builder::kBold; b.Append("\t"); break;
        case MD_BLOCK_TD: b.Append("\t"); break;
        case MD_BLOCK_TABLE: b.NewLine(); break;
        default: break;
    }
    return 0;
}
int EnterSpan(MD_SPANTYPE type, void* detail, void* userdata) {
    auto& b = *static_cast<Builder*>(userdata);
    switch (type) {
        case MD_SPAN_EM: b.style |= Builder::kItalic; break;
        case MD_SPAN_STRONG: b.style |= Builder::kBold; break;
        case MD_SPAN_CODE: b.style |= Builder::kCode; break;
        case MD_SPAN_DEL: b.style |= Builder::kStrike; break;
        case MD_SPAN_A: { auto* d = static_cast<MD_SPAN_A_DETAIL*>(detail); b.linkTarget.assign(d->href.text, d->href.size); b.linkStart = static_cast<int32>(b.text.size()); b.style |= Builder::kLink; break; }
        case MD_SPAN_IMG: { auto* d = static_cast<MD_SPAN_IMG_DETAIL*>(detail); b.Append("[image: "); b.linkTarget.assign(d->src.text, d->src.size); b.linkStart = static_cast<int32>(b.text.size()); b.style |= Builder::kLink; break; }
        default: break;
    }
    return 0;
}
int LeaveSpan(MD_SPANTYPE type, void*, void* userdata) {
    auto& b = *static_cast<Builder*>(userdata);
    switch (type) {
        case MD_SPAN_EM: b.style &= ~Builder::kItalic; break;
        case MD_SPAN_STRONG: b.style &= ~Builder::kBold; break;
        case MD_SPAN_CODE: b.style &= ~Builder::kCode; break;
        case MD_SPAN_DEL: b.style &= ~Builder::kStrike; break;
        case MD_SPAN_A: b.style &= ~Builder::kLink; b.links.push_back({b.linkStart, static_cast<int32>(b.text.size()), b.linkTarget}); break;
        case MD_SPAN_IMG: b.style &= ~Builder::kLink; b.links.push_back({b.linkStart, static_cast<int32>(b.text.size()), b.linkTarget}); b.Append("]"); break;
        default: break;
    }
    return 0;
}
int Text(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto& b = *static_cast<Builder*>(userdata);
    switch (type) {
        case MD_TEXT_BR: b.Append("\n"); b.Indent(); break;
        case MD_TEXT_SOFTBR: b.Append(" "); break;
        case MD_TEXT_NULLCHAR: break;
        case MD_TEXT_ENTITY: {
            std::string entity(text, size);
            if (entity == "&amp;") b.Append("&"); else if (entity == "&lt;") b.Append("<"); else if (entity == "&gt;") b.Append(">");
            else if (entity == "&quot;") b.Append("\""); else if (entity == "&nbsp;") b.Append(" "); else b.Append(entity);
            break;
        }
        case MD_TEXT_HTML: break;   // raw HTML tags are dropped
        default: {
            std::string piece(text, size);
            if (b.style & Builder::kCodeBlock) {
                // Indent every code line so the block reads as one.
                std::string indented;
                for (size_t i = 0; i < piece.size(); ++i) { if (b.atLineStart) indented += "    "; indented += piece[i]; b.atLineStart = piece[i] == '\n'; }
                b.Append(indented);
            } else b.Append(piece);
        }
    }
    return 0;
}
Builder Parse(const std::string& markdown) {
    Builder b;
    MD_PARSER parser{};
    parser.flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_NOHTMLBLOCKS;
    parser.enter_block = EnterBlock; parser.leave_block = LeaveBlock;
    parser.enter_span = EnterSpan; parser.leave_span = LeaveSpan; parser.text = Text;
    if (md_parse(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &parser, &b) != 0) { b = Builder(); b.Append(markdown); }
    // Trim leading blank lines.
    size_t lead = 0;
    while (lead < b.text.size() && b.text[lead] == '\n') ++lead;
    if (lead) {
        b.text.erase(0, lead);
        for (auto& r : b.runs) { r.start = std::max<int32>(0, r.start - static_cast<int32>(lead)); r.end = std::max<int32>(0, r.end - static_cast<int32>(lead)); }
        for (auto& l : b.links) { l.start = std::max<int32>(0, l.start - static_cast<int32>(lead)); l.end = std::max<int32>(0, l.end - static_cast<int32>(lead)); }
    }
    return b;
}
}
std::string MarkdownToPlainText(const std::string& markdown) { return Parse(markdown).text; }
std::vector<MarkdownLink> SetMarkdown(BTextView* view, const std::string& markdown, const MarkdownStyle& style) {
    Builder b = Parse(markdown);
    view->SetStylable(true);
    view->SetText(b.text.c_str());
    BFont plain(be_plain_font), bold(be_bold_font), italic(be_plain_font), fixed(be_fixed_font), boldItalic(be_bold_font);
    plain.SetSize(style.baseSize); bold.SetSize(style.baseSize); fixed.SetSize(style.baseSize);
    italic.SetSize(style.baseSize); italic.SetFace(B_ITALIC_FACE);
    boldItalic.SetSize(style.baseSize); boldItalic.SetFace(B_BOLD_FACE | B_ITALIC_FACE);
    view->SetFontAndColor(0, static_cast<int32>(b.text.size()), &plain, B_FONT_ALL, &style.text);
    for (const auto& run : b.runs) {
        if (run.end <= run.start) continue;
        BFont font = plain;
        rgb_color color = style.text;
        if (run.style & Builder::kCode) { font = fixed; color = style.code; }
        else if ((run.style & Builder::kBold) && (run.style & Builder::kItalic)) font = boldItalic;
        else if (run.style & Builder::kBold) font = bold;
        else if (run.style & Builder::kItalic) font = italic;
        if (run.style & Builder::kHeading) {
            font = bold; font.SetSize(style.baseSize * 1.35f);
        }
        if (run.style & Builder::kQuote) color = style.quote;
        if (run.style & Builder::kLink) { color = style.link; font.SetFace(font.Face() | B_UNDERSCORE_FACE); }
        if (run.style & Builder::kStrike) font.SetFace(font.Face() | B_STRIKEOUT_FACE);
        view->SetFontAndColor(run.start, run.end, &font, B_FONT_ALL, &color);
    }
    return b.links;
}
}
