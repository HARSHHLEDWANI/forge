#pragma once

#include <optional>
#include <string_view>

namespace forge::domain {

// A strict total order (Read < Write < Admin), matching the familiar
// GitHub-style permission ladder rather than a general capability/ACL
// system — that's the whole "RBAC" ask here (implementation-plan.md
// Phase 14), and a project with no issues/PRs/branch-protection yet
// (those are Phase 16) has no finer-grained permission to express.
enum class Role { Read, Write, Admin };

// Whether `actual` satisfies a check that requires at least `required`.
bool role_satisfies(Role actual, Role required);

std::string_view role_to_string(Role role);
std::optional<Role> parse_role(std::string_view text);

} // namespace forge::domain
