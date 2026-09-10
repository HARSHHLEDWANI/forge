#pragma once

#include <filesystem>
#include <optional>

namespace forge::storage {

// Exclusive, whole-repository advisory lock at ".forge/repo.lock",
// acquired for the duration of any command that mutates repository state
// (add, commit, branch, switch, checkout, merge) — the same role Git's
// own ".git/index.lock" plays. This guards against two `forge` processes
// racing each other on the same repository; it's a different concern
// from the ref layer's own compare-and-swap (ADR 0003), which protects a
// single ref file's value but not, say, the working-tree writes a
// checkout or merge makes alongside it.
//
// RAII: acquired in the constructor, released in the destructor, so a
// core::ForgeError thrown by the guarded operation still releases the
// lock during stack unwinding. Movable (so e.g. a small "acquire and
// report" helper can return one) but not copyable — only one RepoLock
// may ever own a given lock file at a time.
class RepoLock {
public:
    // Throws core::ForgeError if the lock is currently held by another
    // live process. If the lock file exists but names a process that
    // isn't running any more (the previous holder crashed, or was killed,
    // without releasing it), the stale lock is removed automatically and
    // acquisition proceeds — recorded in `recovered_stale_pid` so the
    // caller can report it if it wants to.
    explicit RepoLock(std::filesystem::path forge_dir);
    ~RepoLock();

    RepoLock(const RepoLock&) = delete;
    RepoLock& operator=(const RepoLock&) = delete;
    RepoLock(RepoLock&& other) noexcept;
    RepoLock& operator=(RepoLock&& other) noexcept;

    // Set only when construction had to clear a stale lock first.
    std::optional<long> recovered_stale_pid;

private:
    std::filesystem::path lock_path_;
    bool held_ = true; // false once moved-from, so its destructor no-ops
};

} // namespace forge::storage
