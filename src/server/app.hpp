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
// health/version checks aside, every route takes a `repo` query
// parameter naming one of them.
void wire_routes(transport::HttpServer& server, const std::filesystem::path& repos_root);

} // namespace forge::server
