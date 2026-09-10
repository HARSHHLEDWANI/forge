#include "core/remote_protocol.hpp"

#include <sstream>

namespace forge::core {

std::string encode_remote_refs(const RemoteRefs& refs) {
    std::ostringstream out;
    out << "{\"branches\":{";
    bool first = true;
    for (const auto& [name, id] : refs.branches) {
        if (!first) {
            out << ',';
        }
        out << '"' << name << "\":\"" << id.to_hex() << '"';
        first = false;
    }
    out << "},\"head_branch\":";
    if (refs.head_branch) {
        out << '"' << *refs.head_branch << '"';
    } else {
        out << "null";
    }
    out << '}';
    return out.str();
}

std::optional<RemoteRefs> decode_remote_refs(std::string_view json) {
    RemoteRefs result;

    static constexpr std::string_view kBranchesMarker = "\"branches\":{";
    const std::size_t branches_start = json.find(kBranchesMarker);
    if (branches_start == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t body_start = branches_start + kBranchesMarker.size();
    const std::size_t branches_end = json.find('}', body_start);
    if (branches_end == std::string_view::npos) {
        return std::nullopt;
    }

    std::string_view branches_body = json.substr(body_start, branches_end - body_start);
    while (!branches_body.empty()) {
        if (branches_body.front() == ',') {
            branches_body.remove_prefix(1);
            continue;
        }
        if (branches_body.front() != '"') {
            return std::nullopt;
        }
        const std::size_t name_end = branches_body.find('"', 1);
        if (name_end == std::string_view::npos) {
            return std::nullopt;
        }
        const std::string name(branches_body.substr(1, name_end - 1));
        branches_body.remove_prefix(name_end + 1);

        if (branches_body.empty() || branches_body.front() != ':') {
            return std::nullopt;
        }
        branches_body.remove_prefix(1);
        if (branches_body.empty() || branches_body.front() != '"') {
            return std::nullopt;
        }
        const std::size_t value_end = branches_body.find('"', 1);
        if (value_end == std::string_view::npos) {
            return std::nullopt;
        }
        const std::optional<ObjectId> id = ObjectId::parse(branches_body.substr(1, value_end - 1));
        if (!id) {
            return std::nullopt;
        }
        result.branches.insert_or_assign(name, *id);
        branches_body.remove_prefix(value_end + 1);
    }

    static constexpr std::string_view kHeadMarker = "\"head_branch\":";
    const std::size_t head_marker_pos = json.find(kHeadMarker, branches_end);
    if (head_marker_pos == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view after_head = json.substr(head_marker_pos + kHeadMarker.size());
    if (after_head.rfind("null", 0) == 0) {
        result.head_branch = std::nullopt;
    } else if (!after_head.empty() && after_head.front() == '"') {
        const std::size_t end_quote = after_head.find('"', 1);
        if (end_quote == std::string_view::npos) {
            return std::nullopt;
        }
        result.head_branch = std::string(after_head.substr(1, end_quote - 1));
    } else {
        return std::nullopt;
    }

    return result;
}

std::string encode_push_ref_request(const PushRefRequest& request) {
    std::ostringstream out;
    out << "{\"expected_old\":";
    if (request.expected_old) {
        out << '"' << request.expected_old->to_hex() << '"';
    } else {
        out << "null";
    }
    out << ",\"new\":\"" << request.new_commit.to_hex() << '"';
    out << ",\"force\":" << (request.force ? "true" : "false");
    out << '}';
    return out.str();
}

std::optional<PushRefRequest> decode_push_ref_request(std::string_view json) {
    PushRefRequest result;

    static constexpr std::string_view kExpectedOldMarker = "\"expected_old\":";
    const std::size_t expected_old_pos = json.find(kExpectedOldMarker);
    if (expected_old_pos == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view after_expected_old = json.substr(expected_old_pos + kExpectedOldMarker.size());
    if (after_expected_old.rfind("null", 0) == 0) {
        result.expected_old = std::nullopt;
    } else if (!after_expected_old.empty() && after_expected_old.front() == '"') {
        const std::size_t end_quote = after_expected_old.find('"', 1);
        if (end_quote == std::string_view::npos) {
            return std::nullopt;
        }
        const std::optional<ObjectId> id = ObjectId::parse(after_expected_old.substr(1, end_quote - 1));
        if (!id) {
            return std::nullopt;
        }
        result.expected_old = id;
    } else {
        return std::nullopt;
    }

    static constexpr std::string_view kNewMarker = "\"new\":\"";
    const std::size_t new_pos = json.find(kNewMarker);
    if (new_pos == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t new_value_start = new_pos + kNewMarker.size();
    const std::size_t new_value_end = json.find('"', new_value_start);
    if (new_value_end == std::string_view::npos) {
        return std::nullopt;
    }
    const std::optional<ObjectId> new_commit = ObjectId::parse(json.substr(new_value_start, new_value_end - new_value_start));
    if (!new_commit) {
        return std::nullopt;
    }
    result.new_commit = *new_commit;

    static constexpr std::string_view kForceMarker = "\"force\":";
    const std::size_t force_pos = json.find(kForceMarker);
    if (force_pos == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view after_force = json.substr(force_pos + kForceMarker.size());
    if (after_force.rfind("true", 0) == 0) {
        result.force = true;
    } else if (after_force.rfind("false", 0) == 0) {
        result.force = false;
    } else {
        return std::nullopt;
    }

    return result;
}

} // namespace forge::core
