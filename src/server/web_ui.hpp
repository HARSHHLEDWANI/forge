#pragma once

#include <filesystem>
#include <string>

#include "transport/http_server.hpp"

namespace forge::server {

// A small set of read-only, server-rendered HTML pages (repo list,
// issue/PR lists and detail views) over the same data Phase 16's JSON
// API (collaboration_routes.hpp) exposes — plain hand-built HTML
// strings, no template engine or client-side framework, matching this
// codebase's existing "hand-roll it" approach to JSON responses. A
// no-op if `database_url` is empty, same reasoning as
// wire_collaboration_routes. These pages are unauthenticated (read-
// only, same as GET /refs and GET /object already are); mutating the
// data behind them goes through the JSON API's Bearer-token routes —
// a future JS frontend calling those same endpoints is the natural
// next step, not something this hand-rolled HTML needs to anticipate.
void wire_web_ui(transport::HttpServer& server, const std::filesystem::path& repos_root, const std::string& database_url);

} // namespace forge::server
