// Markdown structure checks for the pull request renderer (needs the Interface Kit).
#include "ui/MarkdownText.h"
#include <Application.h>
#include <cstdio>
#include <string>
static int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
int main() {
    BApplication app("application/x-vnd.TurboChook-markdown-tests");
    using tc::MarkdownToPlainText;
    std::string text = MarkdownToPlainText("## What was broken\n\nClicking the *links* replaced the `page`.\n\n- Add `target`\n- Preserve **existing** behavior\n\n1. first\n2. second\n\n> quoted\n\n```\ncode line\n```\n");
    CHECK(text.find("What was broken\n") != std::string::npos);
    CHECK(text.find("Clicking the links replaced the page.") != std::string::npos);
    CHECK(text.find("\xe2\x80\xa2 Add target") != std::string::npos);
    CHECK(text.find("1. first") != std::string::npos && text.find("2. second") != std::string::npos);
    CHECK(text.find("\xe2\x94\x82 quoted") != std::string::npos);
    CHECK(text.find("    code line") != std::string::npos);
    CHECK(text.find("##") == std::string::npos && text.find("**") == std::string::npos && text.find("`") == std::string::npos);
    std::string html = MarkdownToPlainText("Plain <b>bold</b> text &amp; more");
    CHECK(html.find("<b>") == std::string::npos && html.find("&") != std::string::npos);
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
