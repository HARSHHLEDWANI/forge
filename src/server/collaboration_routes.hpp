#pragma once

#include <filesystem>
#include <string>

#include "transport/http_server.hpp"

namespace forge::server {

// Registers Phase 16's collaboration API — issues, comments, labels,
// pull requests, reviews, merging, and branch protection — backed by
// PostgreSQL (database/collaboration_directory.hpp). A no-op if
// `database_url` is empty: these features need a real database (see
// docs/adr/0004), unlike the git/auth routes app.hpp wires
// unconditionally, so a server run without one (or during the plain
// `docker run forge-dev` path, which has none) simply doesn't expose
// them rather than failing every request.
void wire_collaboration_routes(
    transport::HttpServer& server, const std::filesystem::path& repos_root, const std::filesystem::path& data_root,
    const std::string& database_url);

} // namespace forge::server
