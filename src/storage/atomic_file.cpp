#include "storage/atomic_file.hpp"

#include <algorithm>
#include <cstdio>
#include <random>
#include <sstream>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
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
}

} // namespace forge::storage
