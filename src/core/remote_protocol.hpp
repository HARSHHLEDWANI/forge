#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "core/object_id.hpp"

namespace forge::core {

struct RemoteRefs {
    std::map<std::string, ObjectId> branches;
    std::optional<std::string> head_branch; // nullopt if HEAD is detached
};

// Minimal, exact-shape (de)serializers for this project's own
// client<->server wire format (see server/app.cpp and core/remote.hpp) —
// not a general JSON library. Each decode_* only ever needs to parse the
// one fixed shape its matching encode_* produces, so a small hand-rolled
// reader is enough (same "narrow before general" tradeoff as
// verify.cpp's reachability walk or diff.hpp's single-hunk unified
// diff). Safe against the values actually going into these fields: every
// branch/repo name that reaches here has already passed
// branching.hpp's/repo_registry.hpp's validation, which rejects quotes,
// backslashes, and control characters specifically so nothing here ever
// needs to escape or decode an escape sequence — decode_* return nullopt
// rather than guess at anything that doesn't look like what encode_*
// would have produced.
std::string encode_remote_refs(const RemoteRefs& refs);
std::optional<RemoteRefs> decode_remote_refs(std::string_view json);

struct PushRefRequest {
    std::optional<ObjectId> expected_old;
    ObjectId new_commit{Sha256Digest{}}; // overwritten by decode_push_ref_request; ObjectId has no default state of its own
    bool force = false;
};

std::string encode_push_ref_request(const PushRefRequest& request);
std::optional<PushRefRequest> decode_push_ref_request(std::string_view json);

} // namespace forge::core
