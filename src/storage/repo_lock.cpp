#include "storage/repo_lock.hpp"

#include <cstdio>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "core/error.hpp"
#include "storage/repository.hpp"

namespace forge::storage {

namespace {

long current_pid() {
#if defined(_WIN32)
    return static_cast<long>(GetCurrentProcessId());
#else
    return static_cast<long>(getpid());
#endif
}

// Whether `pid` still names a running process. Best-effort: this is
// exactly the existence check the OS itself offers (Windows' handle open,
// POSIX's null signal), same category of "advisory, not airtight" as the
// lock file itself.
bool process_is_alive(long pid) {
#if defined(_WIN32)
    const HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (handle == nullptr) {
        return false;
    }
    DWORD exit_code = 0;
    const bool got_code = GetExitCodeProcess(handle, &exit_code) != 0;
    CloseHandle(handle);
    return got_code && exit_code == STILL_ACTIVE;
#else
    // Signal 0 sends nothing but still performs the existence/permission
    // check: ESRCH means no such process; any other outcome (success, or
    // EPERM for a live process we merely don't own) means it's alive.
    if (kill(static_cast<pid_t>(pid), 0) == 0) {
        return true;
    }
    return errno != ESRCH;
#endif
}

std::optional<long> read_lock_pid(const std::filesystem::path& lock_path) {
    std::ifstream in(lock_path);
    long pid = 0;
    if (!(in >> pid)) {
        return std::nullopt;
    }
    return pid;
}

} // namespace

RepoLock::RepoLock(std::filesystem::path forge_dir) : lock_path_(std::move(forge_dir) / kLockFileName) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        // "wx": exclusive creation — fails rather than truncating if the
        // file already exists, so two processes racing here can never
        // both believe they hold the lock.
        std::FILE* file = std::fopen(lock_path_.string().c_str(), "wx");
        if (file != nullptr) {
            std::fprintf(file, "%ld\n", current_pid());
            std::fclose(file);
            return; // acquired
        }

        const std::optional<long> holder_pid = read_lock_pid(lock_path_);
        if (holder_pid && process_is_alive(*holder_pid)) {
            throw core::ForgeError(
                "repository is locked by another forge process (pid " + std::to_string(*holder_pid) +
                "); if you're sure no other forge process is running, delete " + lock_path_.string() +
                " manually");
        }

        // Stale lock (the holder's process is gone) or unreadable
        // garbage — safe to clear and retry acquiring once.
        std::error_code ec;
        std::filesystem::remove(lock_path_, ec);
        recovered_stale_pid = holder_pid;
    }
    throw core::ForgeError("failed to acquire repository lock: " + lock_path_.string());
}

RepoLock::RepoLock(RepoLock&& other) noexcept
    : recovered_stale_pid(other.recovered_stale_pid), lock_path_(std::move(other.lock_path_)),
      held_(other.held_) {
    other.held_ = false;
}

RepoLock& RepoLock::operator=(RepoLock&& other) noexcept {
    if (this != &other) {
        if (held_) {
            std::error_code ec;
            std::filesystem::remove(lock_path_, ec);
        }
        recovered_stale_pid = other.recovered_stale_pid;
        lock_path_ = std::move(other.lock_path_);
        held_ = other.held_;
        other.held_ = false;
    }
    return *this;
}

RepoLock::~RepoLock() {
    if (!held_) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(lock_path_, ec);
}

} // namespace forge::storage
