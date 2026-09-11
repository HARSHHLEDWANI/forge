#include "server/app.hpp"

#include <chrono>
#include <memory>
#include <optional>

#include "core/error.hpp"
#include "core/merge.hpp"
#include "core/object_encoding.hpp"
#include "core/object_id.hpp"
#include "core/remote_protocol.hpp"
#include "core/version.hpp"
#include "domain/permissions.hpp"
#include "domain/ssh_key.hpp"
#include "domain/token.hpp"
#include "server/collaboration_routes.hpp"
#include "server/rate_limiter.hpp"
#include "server/repo_registry.hpp"
#include "server/web_ui.hpp"
#include "storage/auth_store.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"
#include "transport/http_parser.hpp"

namespace forge::server {

namespace {

std::string query_value(const transport::HttpRequest& request, const std::string& key) {
    const std::map<std::string, std::string> params = transport::parse_query_string(request.query);
    const auto it = params.find(key);
    return it == params.end() ? std::string() : it->second;
}

transport::HttpResponse internal_error(const core::ForgeError& e) {
    return transport::plain_text_response(500, "Internal Server Error", std::string(e.what()) + "\n");
}

std::int64_t now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Extracts and verifies the "Authorization: Bearer <id>.<secret>" header,
// returning the authenticated username. nullopt for a missing/malformed
// header or a token that doesn't verify — the caller decides what that
// means for the request at hand (some routes require it, most don't).
std::optional<std::string> authenticate(const transport::HttpRequest& request, storage::AuthStore& auth_store) {
    const std::optional<std::string> header = transport::find_header(request.headers, "authorization");
    if (!header || header->rfind("Bearer ", 0) != 0) {
        return std::nullopt;
    }
    return auth_store.authenticate_token(header->substr(std::string_view("Bearer ").size()), now_unix());
}

} // namespace

void wire_routes(
    transport::HttpServer& server, const std::filesystem::path& repos_root, const std::filesystem::path& data_root,
    const std::string& database_url) {
    wire_collaboration_routes(server, repos_root, data_root, database_url);
    wire_web_ui(server, repos_root, database_url);

    // 10 attempts, refilled at 1/sec — generous for a real user mistyping
    // a password a few times, tight enough to make credential-stuffing
    // against /login and account creation spam against /users slow.
    // Shared (not per-lambda) so every route below debits the same
    // per-IP bucket; safe with no locking since HttpServer's accept loop
    // is single-threaded (see rate_limiter.hpp).
    auto login_rate_limiter = std::make_shared<RateLimiter>(10, 1.0);

    server.route("GET", "/healthz", [](const transport::HttpRequest&) {
        return transport::json_response(200, "OK", R"({"status":"ok"})");
    });

    server.route("GET", "/version", [](const transport::HttpRequest&) {
        return transport::json_response(
            200, "OK", std::string(R"({"version":")") + std::string(core::kVersion) + "\"}");
    });

    server.route("POST", "/users", [data_root, login_rate_limiter](const transport::HttpRequest& request) {
        try {
            if (!login_rate_limiter->allow(request.remote_address)) {
                return transport::plain_text_response(429, "Too Many Requests", "too many requests; slow down\n");
            }
            storage::AuthStore auth_store(data_root);
            const std::string username = query_value(request, "username");
            if (username.empty() || request.body.empty()) {
                return transport::plain_text_response(400, "Bad Request", "username and a password body are required\n");
            }
            auth_store.create_user(username, request.body, now_unix());
            return transport::json_response(200, "OK", "{\"username\":\"" + username + "\"}");
        } catch (const core::ForgeError& e) {
            return transport::plain_text_response(409, "Conflict", std::string(e.what()) + "\n");
        }
    });

    server.route("POST", "/login", [data_root, login_rate_limiter](const transport::HttpRequest& request) {
        try {
            if (!login_rate_limiter->allow(request.remote_address)) {
                return transport::plain_text_response(429, "Too Many Requests", "too many requests; slow down\n");
            }
            storage::AuthStore auth_store(data_root);
            const std::string username = query_value(request, "username");
            const std::int64_t now = now_unix();
            if (username.empty() || !auth_store.verify_user_password(username, request.body)) {
                auth_store.audit_log().record(now, username.empty() ? "-" : username, "login_failed", "");
                return transport::plain_text_response(401, "Unauthorized", "invalid credentials\n");
            }
            // 24-hour session, matching a typical web login lifetime;
            // domain/token.hpp's TokenKind::PersonalAccessToken (issued
            // separately, not over this endpoint) is the long-lived form.
            constexpr std::int64_t kSessionTtlSeconds = 24 * 60 * 60;
            const std::string token = auth_store.issue_token(username, domain::TokenKind::Session, kSessionTtlSeconds, now);
            auth_store.audit_log().record(now, username, "login_succeeded", "");
            return transport::json_response(200, "OK", "{\"token\":\"" + token + "\"}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("POST", "/ssh-keys", [data_root](const transport::HttpRequest& request) {
        try {
            storage::AuthStore auth_store(data_root);
            const std::optional<std::string> username = authenticate(request, auth_store);
            if (!username) {
                return transport::plain_text_response(401, "Unauthorized", "a valid bearer token is required\n");
            }
            const std::optional<domain::SshPublicKey> key = domain::parse_ssh_public_key(request.body);
            if (!key) {
                return transport::plain_text_response(400, "Bad Request", "malformed SSH public key\n");
            }
            auth_store.add_ssh_key(*username, *key);
            return transport::json_response(
                200, "OK", "{\"fingerprint\":\"" + domain::fingerprint_ssh_public_key(*key) + "\"}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("POST", "/permissions", [repos_root, data_root](const transport::HttpRequest& request) {
        try {
            storage::AuthStore auth_store(data_root);
            const std::optional<std::string> caller = authenticate(request, auth_store);
            if (!caller) {
                return transport::plain_text_response(401, "Unauthorized", "a valid bearer token is required\n");
            }

            const std::string repo_name = query_value(request, "repo");
            const std::string target_username = query_value(request, "username");
            const std::optional<domain::Role> role = domain::parse_role(query_value(request, "role"));
            if (repo_name.empty() || target_username.empty() || !role) {
                return transport::plain_text_response(
                    400, "Bad Request", "repo, username, and a valid role are required\n");
            }

            const RepoRegistry registry(repos_root);
            if (!registry.exists(repo_name)) {
                return transport::plain_text_response(404, "Not Found", "no such repository\n");
            }

            // Bootstrapping: a repo with no permissions recorded yet is
            // "legacy/open" (see the POST /object|/ref doc comments
            // below) — its creator becomes Admin the moment anyone
            // grants the first permission on it, since there's no
            // caller to already hold Admin at that point. Once any
            // permission exists, only an existing Admin may grant more.
            if (auth_store.repo_has_any_permission(repo_name)) {
                const std::optional<domain::Role> caller_role = auth_store.get_permission(repo_name, *caller);
                if (!caller_role || !domain::role_satisfies(*caller_role, domain::Role::Admin)) {
                    return transport::plain_text_response(403, "Forbidden", "admin access is required\n");
                }
            }

            auth_store.set_permission(repo_name, target_username, *role, now_unix());
            return transport::json_response(
                200, "OK",
                "{\"repo\":\"" + repo_name + "\",\"username\":\"" + target_username + "\",\"role\":\"" +
                    std::string(domain::role_to_string(*role)) + "\"}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("GET", "/refs", [repos_root](const transport::HttpRequest& request) {
        try {
            const RepoRegistry registry(repos_root);
            const std::string repo_name = query_value(request, "repo");
            if (repo_name.empty() || !registry.exists(repo_name)) {
                return transport::plain_text_response(404, "Not Found", "no such repository\n");
            }
            storage::RefStore refs(registry.forge_dir_for(repo_name));

            core::RemoteRefs payload;
            for (const std::string& branch : refs.list_branches()) {
                if (const std::optional<core::ObjectId> commit_id = refs.read_branch(branch)) {
                    payload.branches.insert_or_assign(branch, *commit_id);
                }
            }
            payload.head_branch = refs.read_head().branch;

            return transport::json_response(200, "OK", core::encode_remote_refs(payload));
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("GET", "/object", [repos_root](const transport::HttpRequest& request) {
        try {
            const RepoRegistry registry(repos_root);
            const std::string repo_name = query_value(request, "repo");
            if (repo_name.empty() || !registry.exists(repo_name)) {
                return transport::plain_text_response(404, "Not Found", "no such repository\n");
            }
            const std::optional<core::ObjectId> id = core::ObjectId::parse(query_value(request, "id"));
            if (!id) {
                return transport::plain_text_response(400, "Bad Request", "invalid object id\n");
            }

            const storage::RepositoryConfig config = storage::load_config(registry.forge_dir_for(repo_name));
            const storage::ObjectStore objects(config.storage_root);
            if (!objects.contains(*id)) {
                return transport::plain_text_response(404, "Not Found", "no such object\n");
            }
            const storage::StoredObject stored = objects.get(*id);

            transport::HttpResponse response;
            response.status = 200;
            response.status_text = "OK";
            response.headers["content-type"] = "application/octet-stream";
            response.body = core::encode_canonical_object(stored.type, stored.payload);
            return response;
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("POST", "/object", [repos_root, data_root](const transport::HttpRequest& request) {
        try {
            RepoRegistry registry(repos_root);
            const std::string repo_name = query_value(request, "repo");
            if (repo_name.empty()) {
                return transport::plain_text_response(400, "Bad Request", "repo is required\n");
            }
            // A repo that already has at least one recorded permission
            // is access-controlled: writing to it needs Write (or
            // better). A repo with none recorded is "legacy/open" — the
            // model every repo created before Phase 14's endpoints
            // existed (and any anonymous "push to create" today) is
            // in — so this stays backward compatible with Phase 13's
            // clone/fetch/push instead of retroactively locking out
            // every existing repository.
            if (registry.exists(repo_name)) {
                storage::AuthStore auth_store(data_root);
                if (auth_store.repo_has_any_permission(repo_name)) {
                    const std::optional<std::string> username = authenticate(request, auth_store);
                    const std::optional<domain::Role> role =
                        username ? auth_store.get_permission(repo_name, *username) : std::nullopt;
                    if (!role || !domain::role_satisfies(*role, domain::Role::Write)) {
                        return transport::plain_text_response(403, "Forbidden", "write access is required\n");
                    }
                }
            }
            registry.ensure_exists(repo_name); // "push to create" for a genuinely new repo

            const std::optional<core::DecodedObject> decoded = core::decode_canonical_object(request.body);
            if (!decoded) {
                return transport::plain_text_response(400, "Bad Request", "malformed object\n");
            }

            const storage::RepositoryConfig config = storage::load_config(registry.forge_dir_for(repo_name));
            storage::ObjectStore objects(config.storage_root);
            const core::ObjectId id = objects.put(decoded->type, decoded->payload);

            return transport::json_response(200, "OK", "{\"id\":\"" + id.to_hex() + "\"}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });

    server.route("POST", "/ref", [repos_root, data_root](const transport::HttpRequest& request) {
        try {
            RepoRegistry registry(repos_root);
            const std::string repo_name = query_value(request, "repo");
            const std::string branch_name = query_value(request, "branch");
            if (repo_name.empty() || branch_name.empty()) {
                return transport::plain_text_response(400, "Bad Request", "repo and branch are required\n");
            }

            const bool repo_existed_already = registry.exists(repo_name);
            std::optional<std::string> authenticated_username;
            if (repo_existed_already) {
                storage::AuthStore auth_store(data_root);
                if (auth_store.repo_has_any_permission(repo_name)) {
                    authenticated_username = authenticate(request, auth_store);
                    const std::optional<domain::Role> role = authenticated_username
                                                                   ? auth_store.get_permission(repo_name, *authenticated_username)
                                                                   : std::nullopt;
                    if (!role || !domain::role_satisfies(*role, domain::Role::Write)) {
                        return transport::plain_text_response(403, "Forbidden", "write access is required\n");
                    }
                }
            } else {
                storage::AuthStore new_repo_auth_store(data_root);
                authenticated_username = authenticate(request, new_repo_auth_store);
            }
            registry.ensure_exists(repo_name); // "push to create", the common hosting convention

            const std::optional<core::PushRefRequest> push = core::decode_push_ref_request(request.body);
            if (!push) {
                return transport::plain_text_response(400, "Bad Request", "malformed push request\n");
            }

            const storage::RepositoryConfig config = storage::load_config(registry.forge_dir_for(repo_name));
            storage::ObjectStore objects(config.storage_root);
            storage::RefStore refs(registry.forge_dir_for(repo_name));

            if (!objects.contains(push->new_commit)) {
                return transport::plain_text_response(
                    422, "Unprocessable Entity", "unknown commit; push objects before updating the ref\n");
            }

            const std::optional<core::ObjectId> current = refs.read_branch(branch_name);
            std::optional<core::ObjectId> cas_expected = push->expected_old;

            if (current) {
                if (push->force) {
                    // Force overwrites regardless of what the client
                    // thought was there: widen the compare-and-swap's
                    // expectation to the actual current value so it's
                    // guaranteed to succeed below, rather than bypassing
                    // atomicity entirely.
                    cas_expected = current;
                } else {
                    // Non-fast-forward rejection: `current` must actually
                    // be an ancestor of the incoming commit, or this push
                    // would discard history the branch already has —
                    // exactly what --force exists to override.
                    const std::optional<core::ObjectId> base =
                        core::find_merge_base(objects, *current, push->new_commit);
                    if (!base || *base != *current) {
                        return transport::plain_text_response(
                            409, "Conflict", "non-fast-forward update rejected (use --force to override)\n");
                    }
                }
            }

            try {
                refs.update_branch(branch_name, cas_expected, push->new_commit);
            } catch (const core::ForgeError&) {
                // The concurrent-push case (docs/design/failure-scenarios.md
                // item 7): another push landed between our read of
                // `current` and this update.
                return transport::plain_text_response(
                    409, "Conflict", "ref changed concurrently; fetch and try again\n");
            }

            if (!repo_existed_already && authenticated_username) {
                // Bootstrap: whoever authenticated for the push that
                // created this repository becomes its Admin — otherwise
                // no one could ever pass the Admin check POST
                // /permissions needs to grant anyone else access.
                storage::AuthStore auth_store(data_root);
                auth_store.set_permission(repo_name, *authenticated_username, domain::Role::Admin, now_unix());
            }

            return transport::json_response(
                200, "OK", "{\"branch\":\"" + branch_name + "\",\"commit\":\"" + push->new_commit.to_hex() + "\"}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });
}

} // namespace forge::server
