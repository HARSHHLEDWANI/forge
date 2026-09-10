#include "domain/permissions.hpp"

namespace forge::domain {

bool role_satisfies(Role actual, Role required) { return static_cast<int>(actual) >= static_cast<int>(required); }

std::string_view role_to_string(Role role) {
    switch (role) {
        case Role::Read: return "read";
        case Role::Write: return "write";
        case Role::Admin: return "admin";
    }
    return "unknown";
}

std::optional<Role> parse_role(std::string_view text) {
    if (text == "read") return Role::Read;
    if (text == "write") return Role::Write;
    if (text == "admin") return Role::Admin;
    return std::nullopt;
}

} // namespace forge::domain
