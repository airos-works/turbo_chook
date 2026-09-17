#pragma once
#include <GraphicsDefs.h>
#include <string>
#include <vector>
class BTextView;
namespace tc {
// Renders Markdown into a BTextView using font runs: headings, emphasis,
// inline and block code, lists, quotes, links and rules. The parser is md4c;
// no HTML is produced. `linkColor` marks links, whose targets are kept in the
// returned map of text ranges for click handling by the caller.
struct MarkdownLink { int32 start = 0, end = 0; std::string target; };
struct MarkdownStyle {
    rgb_color text, link, code, quote, rule;
    float baseSize = 12;
};
std::vector<MarkdownLink> SetMarkdown(BTextView* view, const std::string& markdown, const MarkdownStyle& style);
// Plain-text conversion used for tests: same block structure, no styling.
std::string MarkdownToPlainText(const std::string& markdown);
}
