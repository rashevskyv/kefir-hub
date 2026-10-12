// Markdown reader for app descriptions and release notes.
// LINK: sphaira/source/markdown_lite.cpp
// LINK: sphaira/source/markdown_html.cpp
#include "markdown_lite.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace sphaira::markdown;

static int failures;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static auto Text(const Block& b) -> std::string {
    std::string s;
    for (const auto& sp : b.spans) {
        s += sp.text;
    }
    return s;
}

static auto Joined(const std::vector<Block>& blocks) -> std::string {
    std::string s;
    for (const auto& b : blocks) {
        s += Text(b) + "\n";
    }
    return s;
}

static void TestHeadingsAndBullets() {
    const auto b = Parse("## What's new\r\n- Fixed crash\r\n  - nested item\r\n1. first\r\n2) second\r\n");
    CHECK(b.size() == 5);
    CHECK(b[0].kind == Kind::Heading && b[0].level == 2 && Text(b[0]) == "What's new");
    CHECK(b[1].kind == Kind::Bullet && b[1].level == 0 && Text(b[1]) == "Fixed crash");
    CHECK(b[2].kind == Kind::Bullet && b[2].level == 1 && Text(b[2]) == "nested item");
    CHECK(b[3].kind == Kind::Numbered && b[3].number == 1 && Text(b[3]) == "first");
    CHECK(b[4].kind == Kind::Numbered && b[4].number == 2 && Text(b[4]) == "second");
}

static void TestHeadingLevels() {
    const auto b = Parse("# A\n### B\n#### C\n###### D\n####### not heading\n#hashtag\n## Title ##\n");
    CHECK(b.size() == 7);
    CHECK(b[0].kind == Kind::Heading && b[0].level == 1);
    CHECK(b[1].kind == Kind::Heading && b[1].level == 3);
    CHECK(b[2].kind == Kind::Heading && b[2].level == 3 && Text(b[2]) == "C");
    CHECK(b[3].kind == Kind::Heading && b[3].level == 3 && Text(b[3]) == "D");
    CHECK(b[4].kind == Kind::Paragraph && Text(b[4]) == "####### not heading");
    CHECK(b[5].kind == Kind::Paragraph && Text(b[5]) == "#hashtag");
    CHECK(b[6].kind == Kind::Heading && Text(b[6]) == "Title");
}

static void TestInlineEmphasis() {
    const auto b = Parse("**bold** and __also__ and *it* and _em_\n");
    CHECK(b.size() == 1);
    const auto& s = b[0].spans;
    CHECK(s.size() == 7);
    CHECK(s[0].text == "bold" && s[0].bold && !s[0].italic);
    CHECK(s[1].text == " and " && !s[1].bold);
    CHECK(s[2].text == "also" && s[2].bold);
    CHECK(s[4].text == "it" && s[4].italic && !s[4].bold);
    CHECK(s[6].text == "em" && s[6].italic);
    CHECK(Text(b[0]) == "bold and also and it and em");
}

static void TestInlineNoMisfire() {
    CHECK(Text(Parse("my_var_name and snake_case_x\n")[0]) == "my_var_name and snake_case_x");
    CHECK(Text(Parse("5 * 3 * 2\n")[0]) == "5 * 3 * 2");
    CHECK(Text(Parse("**unterminated and *lone\n")[0]) == "**unterminated and *lone");
    CHECK(Text(Parse("a ** b\n")[0]) == "a ** b");
    CHECK(Parse("***\n")[0].kind == Kind::Rule);
}

static void TestBoldItalicMixed() {
    const auto b = Parse("***both*** and **bold *inner* text**\n");
    const auto& s = b[0].spans;
    CHECK(s[0].text == "both" && s[0].bold && s[0].italic);
    CHECK(Text(b[0]) == "both and bold inner text");
    bool inner_italic{};
    for (const auto& sp : s) {
        if (sp.text == "inner") {
            inner_italic = sp.italic && sp.bold;
        }
    }
    CHECK(inner_italic);
}

static void TestCodeSpans() {
    const auto b = Parse("Use `x * y` and ``a`b``. `open\n");
    const auto& s = b[0].spans;
    CHECK(s[1].text == "x * y" && s[1].code);
    CHECK(Text(b[0]) == "Use x * y and a`b. `open");
}

static void TestFencedCode() {
    const auto b = Parse("before\n```cpp\n**not bold**\n  indented # no\n```\nafter\n");
    CHECK(b.size() == 4);
    CHECK(b[1].kind == Kind::Code && Text(b[1]) == "**not bold**");
    CHECK(b[2].kind == Kind::Code && Text(b[2]) == "  indented # no");
    CHECK(b[3].kind == Kind::Paragraph && Text(b[3]) == "after");
}

static void TestFenceAndCodeSpansKeepSource() {
    const auto b = Parse("```\n#include <vector>\n&amp; <b>x</b>\n<!-- open\n```\nSet `<path>` now\n");
    CHECK(b.size() == 4);
    CHECK(b[0].kind == Kind::Code && Text(b[0]) == "#include <vector>");
    CHECK(b[1].kind == Kind::Code && Text(b[1]) == "&amp; <b>x</b>");
    CHECK(b[2].kind == Kind::Code && Text(b[2]) == "<!-- open");
    CHECK(b[3].kind == Kind::Paragraph && Text(b[3]) == "Set <path> now");
    CHECK(b[3].spans.size() == 3 && b[3].spans[1].code && b[3].spans[1].text == "<path>");
    CHECK(Text(Parse("Use `a &amp; b` here\n")[0]) == "Use a &amp; b here");
    CHECK(Text(Parse("  code  |  x\n")[0]) == "code  |  x");
}

static void TestUnclosedFence() {
    const auto b = Parse("~~~\nline one\n\nline two\n");
    CHECK(b.size() == 3);
    CHECK(b[0].kind == Kind::Code && Text(b[0]) == "line one");
    CHECK(b[1].kind == Kind::Code && Text(b[1]).empty());
    CHECK(b[2].kind == Kind::Code && Text(b[2]) == "line two");
}

static void TestLinksAndImages() {
    const auto b = Parse("See [the **wiki**](https://x.example/a(b)) ![shot](https://img/x.png) https://bare.example\n");
    CHECK(Text(b[0]) == "See the wiki  https://bare.example");
    CHECK(b[0].spans.size() >= 2);
    CHECK(Text(Parse("[unclosed(link\n")[0]) == "[unclosed(link");
}

static void TestHtml() {
    const auto b = Parse("<p align=\"center\"><img src=\"a.png\" width=\"200\"></p>\n"
                         "Line one<br>Line two<br/>\n"
                         "<details><summary>More</summary>hidden</details>\n"
                         "<b>strong</b> <a href=\"x\">link text</a> &lt;tag&gt; &amp; &quot;q&quot; &#39;s&#39; &#x41;\n"
                         "<!-- hidden comment -->visible\n"
                         "<https://auto.example>\n");
    const auto all = Joined(b);
    CHECK(all.find("img") == std::string::npos);
    CHECK(all.find("hidden comment") == std::string::npos);
    CHECK(all.find("Line one") != std::string::npos && all.find("Line two") != std::string::npos);
    CHECK(all.find("More") != std::string::npos && all.find("hidden") != std::string::npos);
    CHECK(all.find("**") == std::string::npos);
    CHECK(all.find("link text") != std::string::npos && all.find("href") == std::string::npos);
    CHECK(all.find("<tag> & \"q\" 's' A") != std::string::npos);
    CHECK(all.find("https://auto.example") != std::string::npos);
    CHECK(all.find("visible") != std::string::npos);
}

static void TestTables() {
    const auto b = Parse("| Name | Size |\n|------|:----:|\n| Game | 2 GB |\nName | Size\n---|---\n");
    CHECK(b.size() == 3);
    CHECK(Text(b[0]) == "Name  |  Size");
    CHECK(Text(b[1]) == "Game  |  2 GB");
    CHECK(Text(b[2]) == "Name  |  Size");
}

static void TestQuotesRulesAndTasks() {
    const auto b = Parse("> quoted **text**\n> > nested\n---\n- [ ] todo\n- [x] done\n");
    CHECK(b[0].kind == Kind::Quote && Text(b[0]) == "quoted text");
    CHECK(b[1].kind == Kind::Quote && Text(b[1]) == "nested");
    CHECK(b[2].kind == Kind::Rule);
    CHECK(b[3].kind == Kind::Bullet && Text(b[3]) == "[ ] todo");
    CHECK(b[4].kind == Kind::Bullet && Text(b[4]) == "[x] done");
}

static void TestBlankLinesAndEntities() {
    const auto b = Parse("\n\n\nfirst\r\n\r\n\r\n\r\nsecond\n\n\n\n");
    CHECK(b.size() == 3);
    CHECK(b[0].kind == Kind::Paragraph && Text(b[0]) == "first");
    CHECK(b[1].kind == Kind::Paragraph && b[1].spans.empty());
    CHECK(b[2].kind == Kind::Paragraph && Text(b[2]) == "second");
    CHECK(Text(Parse("a &unknown; &#0; &#xD800; b\n")[0]) == "a &unknown; &#0; &#xD800; b");
}

static void TestBurnout3Style() {
    const std::string body =
        "## Burnout 3 Patch Notes\r\n"
        "\r\n"
        "**Important:** update the title before you play.\r\n"
        "\r\n"
        "### Changes\r\n"
        "- Fixed *crash* on startup\r\n"
        "- Improved `frame pacing`\r\n"
        "  - Sub item with [a link](https://example.com/x)\r\n"
        "\r\n"
        "![screenshot](https://github.com/u/r/assets/1.png)\r\n"
        "<p><img src=\"https://x/y.png\"></p>\r\n"
        "\r\n"
        "| Platform | Status |\r\n"
        "| --- | --- |\r\n"
        "| Switch | OK |\r\n"
        "\r\n"
        "---\r\n"
        "Thanks :tada: to **everyone** who reported bugs & ideas.\r\n";

    const auto b = Parse(body);
    CHECK(b[0].kind == Kind::Heading && b[0].level == 2 && Text(b[0]) == "Burnout 3 Patch Notes");
    CHECK(b[1].kind == Kind::Paragraph && b[1].spans.empty());
    CHECK(b[2].kind == Kind::Paragraph && b[2].spans[0].bold && b[2].spans[0].text == "Important:");
    const auto all = Joined(b);
    CHECK(all.find("**") == std::string::npos);
    CHECK(all.find("##") == std::string::npos);
    CHECK(all.find("](") == std::string::npos);
    CHECK(all.find("img") == std::string::npos);
    CHECK(all.find("a link") != std::string::npos);
    CHECK(all.find("Platform  |  Status") != std::string::npos);
    CHECK(all.find("---") == std::string::npos);
    CHECK(all.find("bugs & ideas") != std::string::npos);
    bool crash_italic{};
    for (const auto& blk : b) {
        for (const auto& sp : blk.spans) {
            if (sp.text == "crash") {
                crash_italic = sp.italic;
            }
        }
    }
    CHECK(crash_italic);
}

static void TestMalformedNeverCrashes() {
    const std::string cases[] = {
        "", "\n", "`", "```", "[", "](", "![", "**", "*", "_", "<", "<b", "&", "&#", "\\",
        "- ", "1.", "# ", "> ", "|", "||", "\\*\\_", std::string(5000, '*'), std::string(5000, '['),
        std::string(4096, '`') + "x", std::string(4096, '<') + ">",
    };
    for (const auto& c : cases) {
        (void)Parse(c);
    }
    CHECK(Parse("").empty());
}

int main() {
    TestHeadingsAndBullets();
    TestHeadingLevels();
    TestInlineEmphasis();
    TestInlineNoMisfire();
    TestBoldItalicMixed();
    TestCodeSpans();
    TestFencedCode();
    TestFenceAndCodeSpansKeepSource();
    TestUnclosedFence();
    TestLinksAndImages();
    TestHtml();
    TestTables();
    TestQuotesRulesAndTasks();
    TestBlankLinesAndEntities();
    TestBurnout3Style();
    TestMalformedNeverCrashes();

    if (failures) {
        std::printf("test_markdown_lite: %d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("test_markdown_lite: ok\n");
    return EXIT_SUCCESS;
}
