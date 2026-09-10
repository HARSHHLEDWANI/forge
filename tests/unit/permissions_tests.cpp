#include "domain/permissions.hpp"
#include "support/test_framework.hpp"

using forge::domain::parse_role;
using forge::domain::Role;
using forge::domain::role_satisfies;
using forge::domain::role_to_string;

FORGE_TEST_CASE(role_satisfies_is_a_total_order) {
    FORGE_CHECK(role_satisfies(Role::Admin, Role::Read));
    FORGE_CHECK(role_satisfies(Role::Admin, Role::Write));
    FORGE_CHECK(role_satisfies(Role::Admin, Role::Admin));
    FORGE_CHECK(role_satisfies(Role::Write, Role::Read));
    FORGE_CHECK(role_satisfies(Role::Write, Role::Write));
    FORGE_CHECK(!role_satisfies(Role::Write, Role::Admin));
    FORGE_CHECK(!role_satisfies(Role::Read, Role::Write));
    FORGE_CHECK(!role_satisfies(Role::Read, Role::Admin));
}

FORGE_TEST_CASE(role_to_string_and_parse_role_round_trip) {
    for (const Role role : {Role::Read, Role::Write, Role::Admin}) {
        const auto parsed = parse_role(role_to_string(role));
        FORGE_CHECK(parsed.has_value());
        FORGE_CHECK(*parsed == role);
    }
}

FORGE_TEST_CASE(parse_role_rejects_unknown_text) {
    FORGE_CHECK(!parse_role("superuser").has_value());
    FORGE_CHECK(!parse_role("").has_value());
}
