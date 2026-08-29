#pragma once

// RAII temporary directory for filesystem-touching tests. Removed on
// destruction so tests don't leak state into subsequent runs.

#include <filesystem>
#include <random>
#include <sstream>

namespace forge::test {

class TempDir {
public:
    TempDir() {
        std::random_device rd;
        std::ostringstream name;
        name << "forge-test-" << rd();
        path_ = std::filesystem::temp_directory_path() / name.str();
        std::filesystem::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace forge::test
