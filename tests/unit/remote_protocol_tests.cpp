#include "core/object_id.hpp"
#include "core/remote_protocol.hpp"
#include "support/test_framework.hpp"

using forge::core::decode_push_ref_request;
using forge::core::decode_remote_refs;
using forge::core::encode_push_ref_request;
using forge::core::encode_remote_refs;
using forge::core::ObjectId;
using forge::core::PushRefRequest;
using forge::core::RemoteRefs;

namespace {
ObjectId id_of(const std::string& seed) { return ObjectId::of(seed); }
} // namespace

FORGE_TEST_CASE(remote_refs_round_trips_through_encode_and_decode) {
    RemoteRefs refs;
    refs.branches.insert_or_assign("main", id_of("main-tip"));
    refs.branches.insert_or_assign("feature", id_of("feature-tip"));
    refs.head_branch = "main";

    const auto decoded = decode_remote_refs(encode_remote_refs(refs));
    FORGE_CHECK(decoded.has_value());
    FORGE_CHECK(decoded->branches == refs.branches);
    FORGE_CHECK(decoded->head_branch == refs.head_branch);
}

FORGE_TEST_CASE(remote_refs_round_trips_with_no_branches_and_detached_head) {
    RemoteRefs refs;
    refs.head_branch = std::nullopt;

    const auto decoded = decode_remote_refs(encode_remote_refs(refs));
    FORGE_CHECK(decoded.has_value());
    FORGE_CHECK(decoded->branches.empty());
    FORGE_CHECK(!decoded->head_branch.has_value());
}

FORGE_TEST_CASE(decode_remote_refs_rejects_malformed_json) {
    FORGE_CHECK(!decode_remote_refs("not json at all").has_value());
}

FORGE_TEST_CASE(push_ref_request_round_trips_with_an_expected_old) {
    PushRefRequest request;
    request.expected_old = id_of("old");
    request.new_commit = id_of("new");
    request.force = false;

    const auto decoded = decode_push_ref_request(encode_push_ref_request(request));
    FORGE_CHECK(decoded.has_value());
    FORGE_CHECK(decoded->expected_old == request.expected_old);
    FORGE_CHECK(decoded->new_commit == request.new_commit);
    FORGE_CHECK(decoded->force == request.force);
}

FORGE_TEST_CASE(push_ref_request_round_trips_with_no_expected_old_and_force) {
    PushRefRequest request;
    request.expected_old = std::nullopt;
    request.new_commit = id_of("new");
    request.force = true;

    const auto decoded = decode_push_ref_request(encode_push_ref_request(request));
    FORGE_CHECK(decoded.has_value());
    FORGE_CHECK(!decoded->expected_old.has_value());
    FORGE_CHECK(decoded->new_commit == request.new_commit);
    FORGE_CHECK(decoded->force == true);
}

FORGE_TEST_CASE(decode_push_ref_request_rejects_malformed_json) {
    FORGE_CHECK(!decode_push_ref_request("{}").has_value());
}
