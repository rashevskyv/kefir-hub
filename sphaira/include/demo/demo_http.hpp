#pragma once

// DOCS_DEMO builds only: http(s) replies from sdmc:/config/kefir/demo/http/ (rules in demo_http.cpp).

#include "download.hpp"
#include <optional>

namespace sphaira::demo {

// nullopt for anything that is not http(s); otherwise the fixture reply, or a failure like an offline console.
auto HttpReply(const curl::Api& e) -> std::optional<curl::ApiResult>;

} // namespace sphaira::demo
