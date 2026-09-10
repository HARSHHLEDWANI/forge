#include "server/app.hpp"

#include <optional>

#include "core/error.hpp"
#include "core/merge.hpp"
#include "core/object_encoding.hpp"
#include "core/object_id.hpp"
#include "core/remote_protocol.hpp"
#include "core/version.hpp"
#include "server/repo_registry.hpp"
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

} // namespace

void wire_routes(transport::HttpServer& server, const std::filesystem::path& repos_root) {
    server.route("GET", "/healthz", [](const transport::HttpRequest&) {
        return transport::json_response(200, "OK", R"({"status":"ok"})");
    });

    server.route("GET", "/version", [](const transport::HttpRequest&) {
        return transport::json_response(
            200, "OK", std::string(R"({"version":")") + std::string(core::kVersion) + "\"}");
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

    server.route("POST", "/object", [repos_root](const transport::HttpRequest& request) {
        try {
            RepoRegistry registry(repos_root);
            const std::string repo_name = query_value(request, "repo");
            if (repo_name.empty()) {
                return transport::plain_text_response(400, "Bad Request", "repo is required\n");
            }
            // "Push to create" (see server/repo_registry.hpp): a client
            // pushing to a brand-new repo uploads objects before POST
            // /ref ever runs, so this needs the same auto-create GET
            // /refs deliberately does *not* do (a GET must stay free of
            // side effects).
            registry.ensure_exists(repo_name);
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

    server.route("POST", "/ref", [repos_root](const transport::HttpRequest& request) {
        try {
            RepoRegistry registry(repos_root);
            const std::string repo_name = query_value(request, "repo");
            const std::string branch_name = query_value(request, "branch");
            if (repo_name.empty() || branch_name.empty()) {
                return transport::plain_text_response(400, "Bad Request", "repo and branch are required\n");
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

            return transport::json_response(
                200, "OK", "{\"branch\":\"" + branch_name + "\",\"commit\":\"" + push->new_commit.to_hex() + "\"}");
        } catch (const core::ForgeError& e) {
            return internal_error(e);
        }
    });
}

} // namespace forge::server
