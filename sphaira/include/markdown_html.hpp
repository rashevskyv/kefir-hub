#pragma once

#include <string>
#include <string_view>

namespace sphaira::markdown {

// Prepares raw text for the Markdown reader. Removes '\r', HTML comments and tags
// (<br> becomes a line break), decodes entities. Fenced code and backtick code spans
// are copied as they are.
auto Clean(std::string_view raw) -> std::string;

} // namespace sphaira::markdown
