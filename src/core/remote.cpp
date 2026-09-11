#include "core/remote.hpp"

#include <deque>
#include <unordered_set>

#include "core/checkout.hpp"
#include "core/commit.hpp"
#include "core/error.hpp"
#include "core/object_encoding.hpp"
#include "core/tree.hpp"
#include "transport/http_client.hpp"
#include "transport/http_parser.hpp"

namespace forge::core {

namespace {

transport::HttpResponse call(
    const RemoteEndpoint& remote, std::string method, std::string_view route,
    const std::map<std::string, std::string>& params, std::string body = "", std::string_view auth_token = "") {
    transport::HttpClientRequest request;
    request.method = std::move(method);
    request.path = "/" + std::string(route);
    if (!params.empty()) {
        request.path += "?" + transport::render_query_string(params);
    }
    request.body = std::move(body);
    if (!auth_token.empty()) {
        request.headers["authorization"] = "Bearer " + std::string(auth_token);
    }
    return transport::send_http_request(remote.host, remote.port, request);
}

// Every commit/tree/blob id reachable from `start`, added to `out`.
// Assumes every dependency of an id already in `out` is fully present in
// the store — this project's own invariant (an object is never "known"
// without everything it depends on also being known) that's what makes
// negotiation cheap in the first place; see remote.hpp's doc comments.
void collect_reachable(const storage::ObjectStore& objects, const ObjectId& start, std::unordered_set<ObjectId>& out) {
    if (!out.insert(start).second || !objects.contains(start)) {
        return;
    }
    const storage::StoredObject stored = objects.get(start);
    if (stored.type == "commit") {
        const std::optional<Commit> commit = decode_commit(stored.payload);
        if (!commit) {
            return;
        }
        collect_reachable(objects, commit->tree_id, out);
        for (const ObjectId& parent : commit->parent_ids) {
            collect_reachable(objects, parent, out);
        }
    } else if (stored.type == "tree") {
        const std::optional<Tree> tree = decode_tree(stored.payload);
        if (!tree) {
            return;
        }
        for (const TreeEntry& entry : tree->entries()) {
            collect_reachable(objects, entry.id, out);
        }
    }
    // blob: nothing further to walk
}

} // namespace

std::optional<RemoteEndpoint> parse_remote_url(std::string_view url) {
    constexpr std::string_view kScheme = "forge://";
    if (url.rfind(kScheme, 0) != 0) {
        return std::nullopt;
    }
    const std::string_view rest = url.substr(kScheme.size());
    const std::size_t slash = rest.find('/');
    if (slash == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view host_port = rest.substr(0, slash);
    const std::string_view repo_name = rest.substr(slash + 1);
    if (repo_name.empty()) {
        return std::nullopt;
    }

    const std::size_t colon = host_port.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view host = host_port.substr(0, colon);
    const std::string_view port_text = host_port.substr(colon + 1);
    if (host.empty() || port_text.empty()) {
        return std::nullopt;
    }

    int parsed_port = 0;
    try {
        std::size_t consumed = 0;
        parsed_port = std::stoi(std::string(port_text), &consumed);
        if (consumed != port_text.size()) {
            return std::nullopt;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (parsed_port <= 0 || parsed_port > 65535) {
        return std::nullopt;
    }

    return RemoteEndpoint{std::string(host), static_cast<std::uint16_t>(parsed_port), std::string(repo_name)};
}

std::string render_remote_url(const RemoteEndpoint& remote) {
    return "forge://" + remote.host + ":" + std::to_string(remote.port) + "/" + remote.repo_name;
}

std::optional<ServerAddress> parse_server_address(std::string_view url) {
    std::string_view rest = url;
    constexpr std::string_view kScheme = "forge://";
    if (rest.rfind(kScheme, 0) == 0) {
        rest = rest.substr(kScheme.size());
    }
    const std::size_t slash = rest.find('/');
    const std::string_view host_port = slash == std::string_view::npos ? rest : rest.substr(0, slash);

    const std::size_t colon = host_port.find(':');
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view host = host_port.substr(0, colon);
    const std::string_view port_text = host_port.substr(colon + 1);
    if (host.empty() || port_text.empty()) {
        return std::nullopt;
    }

    int parsed_port = 0;
    try {
        std::size_t consumed = 0;
        parsed_port = std::stoi(std::string(port_text), &consumed);
        if (consumed != port_text.size()) {
            return std::nullopt;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (parsed_port <= 0 || parsed_port > 65535) {
        return std::nullopt;
    }

    return ServerAddress{std::string(host), static_cast<std::uint16_t>(parsed_port)};
}

std::string render_server_address(const ServerAddress& server) {
    return "forge://" + server.host + ":" + std::to_string(server.port);
}

std::string login(const ServerAddress& server, std::string_view username, std::string_view password) {
    transport::HttpClientRequest request;
    request.method = "POST";
    request.path = "/login?" + transport::render_query_string({{"username", std::string(username)}});
    request.body = std::string(password);
    const transport::HttpResponse response = transport::send_http_request(server.host, server.port, request);
    if (response.status != 200) {
        throw ForgeError("login failed: " + response.body);
    }

    // The body is `{"token":"<id>.<secret>"}` (server/app.cpp's POST
    // /login) — a hand-parse for this one field rather than pulling in
    // a JSON library, the same call core/remote_protocol.hpp already
    // made for the richer ref/push-request shapes.
    const std::string key = "\"token\":\"";
    const std::size_t start = response.body.find(key);
    if (start == std::string::npos) {
        throw ForgeError("login failed: malformed response from server");
    }
    const std::size_t value_start = start + key.size();
    const std::size_t value_end = response.body.find('"', value_start);
    if (value_end == std::string::npos) {
        throw ForgeError("login failed: malformed response from server");
    }
    return response.body.substr(value_start, value_end - value_start);
}

RemoteRefs fetch(storage::ObjectStore& objects, const RemoteEndpoint& remote, std::string_view auth_token) {
    const transport::HttpResponse refs_response =
        call(remote, "GET", "refs", {{"repo", remote.repo_name}}, "", auth_token);
    if (refs_response.status != 200) {
        throw ForgeError("fetch failed: " + refs_response.body);
    }
    const std::optional<RemoteRefs> remote_refs = decode_remote_refs(refs_response.body);
    if (!remote_refs) {
        throw ForgeError("fetch failed: malformed refs response from remote");
    }

    std::unordered_set<ObjectId> queued;
    std::deque<ObjectId> queue;
    for (const auto& [branch, commit_id] : remote_refs->branches) {
        (void)branch;
        if (!objects.contains(commit_id) && queued.insert(commit_id).second) {
            queue.push_back(commit_id);
        }
    }

    while (!queue.empty()) {
        const ObjectId id = queue.front();
        queue.pop_front();
        if (objects.contains(id)) {
            continue; // became known locally already, e.g. shared by two branches both queued earlier
        }

        const transport::HttpResponse object_response =
            call(remote, "GET", "object", {{"repo", remote.repo_name}, {"id", id.to_hex()}}, "", auth_token);
        if (object_response.status != 200) {
            throw ForgeError("fetch failed: remote is missing object " + id.to_hex());
        }
        const std::optional<DecodedObject> decoded = decode_canonical_object(object_response.body);
        if (!decoded) {
            throw ForgeError("fetch failed: remote sent a malformed object for " + id.to_hex());
        }
        const ObjectId stored_id = objects.put(decoded->type, decoded->payload);
        if (stored_id != id) {
            throw ForgeError("fetch failed: remote's object " + id.to_hex() + " doesn't hash to its own id");
        }

        if (decoded->type == "commit") {
            const std::optional<Commit> commit = decode_commit(decoded->payload);
            if (!commit) {
                throw ForgeError("fetch failed: malformed commit " + id.to_hex());
            }
            if (!objects.contains(commit->tree_id) && queued.insert(commit->tree_id).second) {
                queue.push_back(commit->tree_id);
            }
            for (const ObjectId& parent : commit->parent_ids) {
                if (!objects.contains(parent) && queued.insert(parent).second) {
                    queue.push_back(parent);
                }
            }
        } else if (decoded->type == "tree") {
            const std::optional<Tree> tree = decode_tree(decoded->payload);
            if (!tree) {
                throw ForgeError("fetch failed: malformed tree " + id.to_hex());
            }
            for (const TreeEntry& entry : tree->entries()) {
                if (!objects.contains(entry.id) && queued.insert(entry.id).second) {
                    queue.push_back(entry.id);
                }
            }
        }
        // blob: nothing further to walk
    }

    return *remote_refs;
}

storage::InitResult clone(
    const RemoteEndpoint& remote, const std::filesystem::path& target_dir, std::string_view auth_token) {
    const storage::InitResult init_result = storage::initialize_repository(target_dir);
    const storage::RepositoryConfig config = storage::load_config(init_result.forge_dir);

    storage::ObjectStore objects(config.storage_root);
    const RemoteRefs remote_refs = fetch(objects, remote, auth_token);

    storage::RefStore refs(init_result.forge_dir);
    storage::IndexStore index_store(init_result.forge_dir / storage::kIndexFileName);

    const std::string head_branch = remote_refs.head_branch.value_or(std::string(storage::kDefaultBranchName));
    const auto head_branch_entry = remote_refs.branches.find(head_branch);
    if (head_branch_entry != remote_refs.branches.end()) {
        // Check out before any branch ref exists, while HEAD is still
        // unborn: checkout()'s own safety check compares the target
        // against whatever refs.resolve_head() currently reports, and
        // an empty "before" state is exactly right for a repository
        // this fresh — there's nothing on disk yet to protect. Creating
        // the branch refs first would make HEAD resolve to a real
        // commit prematurely and make that comparison see a false
        // mismatch (checkout would refuse, believing something
        // conflicting was already checked out).
        const CheckoutTarget target{CheckoutTargetKind::Branch, head_branch, head_branch_entry->second};
        checkout(objects, index_store, refs, target_dir, target);
    }
    // else: the remote has no commits yet (or names a branch it never
    // actually reported) — leave the working tree empty; the loop below
    // still creates every branch the remote does have.

    for (const auto& [branch, commit_id] : remote_refs.branches) {
        refs.update_branch(branch, std::nullopt, commit_id);
    }

    return init_result;
}

PushResult push(
    storage::ObjectStore& objects, const storage::RefStore& refs, const RemoteEndpoint& remote,
    std::string_view branch, bool force, std::string_view auth_token) {
    const std::optional<ObjectId> local_tip = refs.read_branch(branch);
    if (!local_tip) {
        throw ForgeError("cannot push: no such local branch: " + std::string(branch));
    }

    // A 404 here means the remote repository doesn't exist yet — not a
    // failure: POST /ref (below) auto-creates it, "push to create" being
    // the common hosting convention (see server/repo_registry.hpp), so
    // this is just an empty ref set to negotiate against, same as a
    // freshly created empty repository would report.
    const transport::HttpResponse refs_response =
        call(remote, "GET", "refs", {{"repo", remote.repo_name}}, "", auth_token);
    std::optional<RemoteRefs> remote_refs;
    if (refs_response.status == 404) {
        remote_refs = RemoteRefs{};
    } else if (refs_response.status == 200) {
        remote_refs = decode_remote_refs(refs_response.body);
        if (!remote_refs) {
            throw ForgeError("push failed: malformed refs response from remote");
        }
    } else {
        throw ForgeError("push failed: " + refs_response.body);
    }
    const auto remote_branch_it = remote_refs->branches.find(std::string(branch));
    const std::optional<ObjectId> remote_tip =
        remote_branch_it == remote_refs->branches.end() ? std::nullopt : std::optional(remote_branch_it->second);

    std::unordered_set<ObjectId> excluded;
    if (remote_tip) {
        collect_reachable(objects, *remote_tip, excluded);
    }
    std::unordered_set<ObjectId> to_upload;
    collect_reachable(objects, *local_tip, to_upload);
    for (const ObjectId& id : excluded) {
        to_upload.erase(id);
    }

    for (const ObjectId& id : to_upload) {
        const storage::StoredObject stored = objects.get(id);
        const std::string raw = encode_canonical_object(stored.type, stored.payload);
        const transport::HttpResponse upload_response =
            call(remote, "POST", "object", {{"repo", remote.repo_name}}, raw, auth_token);
        if (upload_response.status != 200) {
            throw ForgeError("push failed: could not upload object " + id.to_hex() + ": " + upload_response.body);
        }
    }

    PushRefRequest push_request;
    push_request.expected_old = remote_tip;
    push_request.new_commit = *local_tip;
    push_request.force = force;

    const transport::HttpResponse ref_response = call(
        remote, "POST", "ref", {{"repo", remote.repo_name}, {"branch", std::string(branch)}},
        encode_push_ref_request(push_request), auth_token);
    if (ref_response.status != 200) {
        throw ForgeError("push rejected: " + ref_response.body);
    }

    return PushResult{*local_tip, to_upload.size()};
}

} // namespace forge::core
