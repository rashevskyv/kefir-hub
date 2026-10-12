#pragma once

#include <string>
#include <string_view>
#include <vector>

// Small, libnx-free Markdown reader for release notes and app descriptions.
// Turns GitHub-style Markdown into blocks of styled text. It does not render.
namespace sphaira::markdown {

enum class Kind {
    Paragraph,
    Heading, // level: 1..3
    Bullet,  // level: nesting depth (0 = top)
    Numbered, // level: nesting depth, number: the item number
    Quote,
    Code,    // one block per source line, kept verbatim
    Rule,
};

struct Span {
    std::string text;
    bool bold{};
    bool italic{};
    bool code{};
};

struct Block {
    Kind kind{Kind::Paragraph};
    int level{};
    int number{};
    // empty for a Paragraph: it marks one blank gap
    std::vector<Span> spans;
};

// Never fails. Malformed markers stay as literal text.
auto Parse(std::string_view src) -> std::vector<Block>;

} // namespace sphaira::markdown
