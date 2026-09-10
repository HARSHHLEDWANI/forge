#pragma once

#include <filesystem>

#include "transport/http_server.hpp"

namespace forge::server {

// Registers V2's HTTP routes onto an already-constructed HttpServer
// (constructed by the caller — main_server.cpp for the real binary, a
// test for anything that wants to exercise these routes directly —
// rather than this function building and returning one itself, since
// HttpServer owns a live socket and isn't meant to be passed around by
// value).
//
// `repos_root` is where every repository this server hosts lives, one
// subdirectory per repo name (see server/repo_registry.hpp) — plain
// health/version checks aside, every repo route takes a `repo` query
// parameter naming one of them. `data_root` is where user accounts,
// SSH keys, tokens, and permissions live (see storage/auth_store.hpp) —
// server-wide, not per-repo, the same way a real hosting platform's user
// accounts aren't scoped to any one repository.
//
// A repository with no permissions recorded is "legacy/open": every
// repo created before these auth routes existed, and any repo created
// today by an anonymous (unauthenticated) push, stays exactly as
// permissive as Phase 13 left it — write access only starts requiring
// authentication once at least one permission is actually granted on
// that repo (see POST /permissions and POST /ref's bootstrap comment).
void wire_routes(
    transport::HttpServer& server, const std::filesystem::path& repos_root, const std::filesystem::path& data_root);

} // namespace forge::server
