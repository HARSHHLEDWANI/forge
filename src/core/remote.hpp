#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "core/object_id.hpp"
#include "core/remote_protocol.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"

namespace forge::core {

struct RemoteEndpoint {
    std::string host;
    std::uint16_t port;
    std::string repo_name;
};

// A bare "forge://host:port" server address, with no repository name —
// what `forge login`/`forge logout` operate on and what the credential
// store (storage/credential_store.hpp) keys saved tokens by, since a
// token issued by POST /login authenticates a user against the whole
// server (server/app.cpp), not any one repository. Distinct from
// RemoteEndpoint, which always names a specific repo.
struct ServerAddress {
    std::string host;
    std::uint16_t port;
};

// Parses "forge://host:port" or a bare "host:port" — a trailing
// "/anything" (e.g. a repo URL pasted by mistake) is accepted and
// ignored, since the server address is what actually matters here.
// Returns nullopt for a missing/non-numeric port or empty host.
std::optional<ServerAddress> parse_server_address(std::string_view url);

std::string render_server_address(const ServerAddress& server);

// Calls POST /login on `server`, returning the issued bearer token.
// Throws core::ForgeError on wrong credentials or any transport/parse
// failure.
std::string login(const ServerAddress& server, std::string_view username, std::string_view password);

// Parses this project's own minimal remote URL scheme,
// "forge://host:port/repo-name" — not git's ssh/https forms (V2 doesn't
// implement git's wire protocol, just this project's own HTTP+JSON one;
// see docs/adr/0004). Returns nullopt for anything else, including a
// missing/non-numeric port or an empty repo name.
std::optional<RemoteEndpoint> parse_remote_url(std::string_view url);

// Inverse of parse_remote_url — so clone's success message doesn't
// hand-format the scheme itself.
std::string render_remote_url(const RemoteEndpoint& remote);

// Downloads every object reachable from the remote's branches that
// isn't already in `objects` (object negotiation: a BFS from each
// branch tip that stops descending the moment it hits an object already
// present locally — see remote.cpp — since this store's own invariant
// is that an object is never "known" without everything it depends on
// also being known, having it locally is proof its whole subtree is too;
// see server/app.cpp's GET /refs and GET /object). Returns the remote's
// current ref map; never touches `objects`'s own repository's refs or
// working tree — see clone() for that.
// `auth_token`, if non-empty, is sent as "Authorization: Bearer
// <auth_token>" on every request to the remote (see storage/
// credential_store.hpp for where the CLI gets it from) — required for
// GET /object/GET /refs only on a repository with recorded permissions
// (server/app.cpp), harmless to send otherwise.
RemoteRefs fetch(storage::ObjectStore& objects, const RemoteEndpoint& remote, std::string_view auth_token = "");

// Initializes a fresh repository at `target_dir`, fetch()es everything
// the remote's branches reach, creates a local branch for each one the
// remote reports, and checks out whichever branch the remote's HEAD
// names — or leaves an empty working tree on the unborn default branch
// if the remote has no commits yet. Throws core::ForgeError if the
// remote is unreachable or sends something malformed.
storage::InitResult clone(
    const RemoteEndpoint& remote, const std::filesystem::path& target_dir, std::string_view auth_token = "");

struct PushResult {
    ObjectId commit_id;
    std::size_t objects_uploaded;
};

// Uploads every object `branch`'s local history reaches that isn't
// already reachable from the remote's current tip for that branch
// (negotiation computed entirely from the local store: everything the
// remote's last-known tip already covers is, by definition, everything
// it already has — see remote.cpp), then asks the remote to update its
// ref (server/app.cpp's POST /ref does the actual non-fast-forward
// check). `force` bypasses that check the same way `git push --force`
// does. Throws core::ForgeError if `branch` doesn't exist locally, or if
// the remote rejects the update — this never retries or merges on its
// own (cli.md's "no silent resolution of ambiguous merges").
PushResult push(
    storage::ObjectStore& objects, const storage::RefStore& refs, const RemoteEndpoint& remote,
    std::string_view branch, bool force, std::string_view auth_token = "");

} // namespace forge::core
