#include "storage/atomic_file.hpp"

#include <algorithm>
#include <cstdio>
#include <random>
#include <sstream>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "core/error.hpp"

namespace forge::storage {

namespace {

void sync_file_handle(std::FILE* file) {
#if defined(_WIN32)
    const int result = _commit(_fileno(file));
#else
    const int result = fsync(fileno(file));
#endif
    if (result != 0) {
        throw core::ForgeError("failed to sync file to disk");
    }
}

// Durability against real power loss (as opposed to a process crash)
// needs the rename itself to survive too, which means syncing the
// directory entry, not just the file's own bytes — a renamed-but-not-
// yet-durable directory entry can still revert to the old state after a
// crash even though the file content sync above succeeded.
void sync_directory(const std::filesystem::path& dir) {
#if defined(_WIN32)
    // FILE_FLAG_BACKUP_SEMANTICS is what lets CreateFile open a
    // directory at all; FlushFileBuffers on the resulting handle has
    // flushed directory metadata on NTFS since Windows Vista.
    const HANDLE handle = CreateFileW(
        dir.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        throw core::ForgeError("failed to open directory for sync: " + dir.string());
    }
    const BOOL ok = FlushFileBuffers(handle);
    CloseHandle(handle);
    if (!ok) {
        throw core::ForgeError("failed to sync directory: " + dir.string());
    }
#else
    const int fd = open(dir.c_str(), O_RDONLY);
    if (fd < 0) {
        throw core::ForgeError("failed to open directory for sync: " + dir.string());
    }
    const int result = fsync(fd);
    close(fd);
    if (result != 0) {
        throw core::ForgeError("failed to sync directory: " + dir.string());
    }
#endif
}

std::filesystem::path make_temp_path(const std::filesystem::path& target) {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream suffix;
    suffix << std::hex << rng();
    return target.parent_path() / (target.filename().string() + ".tmp-" + suffix.str());
}

struct TempFileGuard {
    std::filesystem::path path;
    bool released = false;

    ~TempFileGuard() {
        if (!released) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }
};

} // namespace

void write_file_atomic(const std::filesystem::path& target, std::string_view data) {
    const std::filesystem::path temp_path = make_temp_path(target);
    TempFileGuard guard{temp_path};

    std::FILE* file = std::fopen(temp_path.string().c_str(), "wb");
    if (file == nullptr) {
        throw core::ForgeError("failed to create temp file: " + temp_path.string());
    }

    const std::size_t written =
        data.empty() ? 0 : std::fwrite(data.data(), 1, data.size(), file);
    if (written != data.size()) {
        std::fclose(file);
        throw core::ForgeError("short write to temp file: " + temp_path.string());
    }

    if (std::fflush(file) != 0) {
        std::fclose(file);
        throw core::ForgeError("failed to flush temp file: " + temp_path.string());
    }

    sync_file_handle(file);

    if (std::fclose(file) != 0) {
        throw core::ForgeError("failed to close temp file: " + temp_path.string());
    }

    // Verify by reading the temp file back before it is ever visible at
    // `target`, per the write -> verify -> publish durability pattern.
    std::FILE* readback = std::fopen(temp_path.string().c_str(), "rb");
    if (readback == nullptr) {
        throw core::ForgeError(
            "failed to reopen temp file for verification: " + temp_path.string());
    }
    std::vector<char> buffer(data.size());
    const std::size_t read_count =
        data.empty() ? 0 : std::fread(buffer.data(), 1, buffer.size(), readback);
    std::fclose(readback);
    if (read_count != data.size() || !std::equal(buffer.begin(), buffer.end(), data.begin())) {
        throw core::ForgeError("verification failed for temp file: " + temp_path.string());
    }

    std::error_code ec;
    std::filesystem::rename(temp_path, target, ec);
    if (ec) {
        throw core::ForgeError("failed to publish file: " + target.string() + ": " + ec.message());
    }

    guard.released = true; // rename succeeded: nothing left to clean up
    sync_directory(target.parent_path());
}

} // namespace forge::storage
