#include <fstream>

#include "core/error.hpp"
#include "storage/repo_lock.hpp"
#include "storage/repository.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::storage::kLockFileName;
using forge::storage::RepoLock;
using forge::test::TempDir;

FORGE_TEST_CASE(repo_lock_creates_and_removes_lock_file) {
    TempDir dir;
    const std::filesystem::path lock_path = dir.path() / kLockFileName;
    {
        RepoLock lock(dir.path());
        FORGE_CHECK(std::filesystem::exists(lock_path));
    }
    FORGE_CHECK(!std::filesystem::exists(lock_path));
}

FORGE_TEST_CASE(repo_lock_rejects_a_second_acquire_while_first_is_held) {
    TempDir dir;
    RepoLock first(dir.path());

    bool threw = false;
    try {
        RepoLock second(dir.path());
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
}

FORGE_TEST_CASE(repo_lock_can_be_reacquired_after_release) {
    TempDir dir;
    { RepoLock first(dir.path()); }
    RepoLock second(dir.path()); // must not throw
    FORGE_CHECK(std::filesystem::exists(dir.path() / kLockFileName));
}

FORGE_TEST_CASE(repo_lock_recovers_a_stale_lock_left_by_a_dead_process) {
    TempDir dir;
    const std::filesystem::path lock_path = dir.path() / kLockFileName;
    {
        // A pid essentially guaranteed not to name a live process: it's
        // far past any real OS's maximum pid (Linux's default pid_max is
        // 32768, or 4194304 at its highest configurable ceiling; Windows
        // pids are similarly bounded), so no live process can hold it.
        std::ofstream stale(lock_path, std::ios::binary);
        stale << "999999999\n";
    }

    RepoLock lock(dir.path());
    FORGE_CHECK(lock.recovered_stale_pid.has_value());
    FORGE_CHECK(*lock.recovered_stale_pid == 999999999);
    FORGE_CHECK(std::filesystem::exists(lock_path)); // re-acquired under our own pid
}

FORGE_TEST_CASE(repo_lock_move_constructs_and_transfers_ownership) {
    TempDir dir;
    const std::filesystem::path lock_path = dir.path() / kLockFileName;

    std::optional<RepoLock> outer;
    {
        RepoLock inner(dir.path());
        outer = std::move(inner);
    }
    FORGE_CHECK(std::filesystem::exists(lock_path)); // still held by `outer`

    outer.reset();
    FORGE_CHECK(!std::filesystem::exists(lock_path)); // released exactly once
}
