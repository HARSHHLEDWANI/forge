#pragma once

// Minimal header-only test framework. Forge avoids an external test
// dependency for now: the surface needed (named cases, checks, a
// pass/fail summary) is small enough to own, and it keeps the build
// fully offline-capable with no fetched sources.
//
// Usage:
//   FORGE_TEST_CASE(some_behavior) {
//       FORGE_CHECK(2 + 2 == 4);
//   }
// and link a single tests/unit/main.cpp calling forge::test::run_all().

#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace forge::test {

struct Failure {
    std::string expression;
    const char* file;
    int line;
};

using CaseFn = std::function<void()>;

inline std::vector<std::pair<std::string, CaseFn>>& registry() {
    static std::vector<std::pair<std::string, CaseFn>> cases;
    return cases;
}

inline std::vector<Failure>& current_failures() {
    static std::vector<Failure> failures;
    return failures;
}

inline void record_failure(Failure failure) {
    current_failures().push_back(std::move(failure));
}

struct Registrar {
    Registrar(std::string name, CaseFn body) {
        registry().emplace_back(std::move(name), std::move(body));
    }
};

inline int run_all() {
    int total = 0;
    int failed = 0;

    for (auto& [name, body] : registry()) {
        ++total;
        current_failures().clear();

        try {
            body();
        } catch (const std::exception& e) {
            record_failure({std::string("uncaught exception: ") + e.what(), "", 0});
        } catch (...) {
            record_failure({"uncaught non-standard exception", "", 0});
        }

        if (current_failures().empty()) {
            std::cout << "[PASS] " << name << '\n';
            continue;
        }

        ++failed;
        std::cerr << "[FAIL] " << name << '\n';
        for (const Failure& failure : current_failures()) {
            std::cerr << "    " << failure.file << ':' << failure.line
                       << ": CHECK(" << failure.expression << ") failed\n";
        }
    }

    std::cout << (total - failed) << '/' << total << " tests passed\n";
    return failed == 0 ? 0 : 1;
}

} // namespace forge::test

#define FORGE_TEST_CASE(unique_name)                                                   \
    static void unique_name();                                                        \
    namespace {                                                                        \
    const ::forge::test::Registrar registrar_##unique_name(#unique_name, unique_name); \
    }                                                                                  \
    static void unique_name()

#define FORGE_CHECK(expr)                                                    \
    do {                                                                     \
        if (!(expr)) {                                                       \
            ::forge::test::record_failure({#expr, __FILE__, __LINE__});      \
        }                                                                    \
    } while (false)
