#include <fstream>
#include <sstream>
#include <string>

#include "core/error.hpp"
#include "storage/atomic_file.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::storage::write_file_atomic;
using forge::test::TempDir;

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

} // namespace

FORGE_TEST_CASE(write_file_atomic_creates_file_with_exact_content) {
    TempDir dir;
    const std::filesystem::path target = dir.path() / "example.txt";
    write_file_atomic(target, "hello forge");
    FORGE_CHECK(std::filesystem::exists(target));
    FORGE_CHECK(read_file(target) == "hello forge");
}

FORGE_TEST_CASE(write_file_atomic_is_binary_safe) {
    TempDir dir;
    const std::filesystem::path target = dir.path() / "binary.bin";
    const std::string data("a\0b\0c", 5);
    write_file_atomic(target, data);
    FORGE_CHECK(read_file(target) == data);
}

FORGE_TEST_CASE(write_file_atomic_overwrites_existing_content) {
    TempDir dir;
    const std::filesystem::path target = dir.path() / "example.txt";
    write_file_atomic(target, "first");
    write_file_atomic(target, "second, longer content");
    FORGE_CHECK(read_file(target) == "second, longer content");
}

FORGE_TEST_CASE(write_file_atomic_leaves_no_temp_file_behind_on_success) {
    TempDir dir;
    const std::filesystem::path target = dir.path() / "example.txt";
    write_file_atomic(target, "data");

    int entry_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir.path())) {
        (void)entry;
        ++entry_count;
    }
    FORGE_CHECK(entry_count == 1); // only "example.txt", no ".tmp-*" leftovers
}

FORGE_TEST_CASE(write_file_atomic_throws_when_parent_directory_missing) {
    TempDir dir;
    const std::filesystem::path target = dir.path() / "missing-parent" / "example.txt";
    bool threw = false;
    try {
        write_file_atomic(target, "data");
    } catch (const forge::core::ForgeError&) {
        threw = true;
    }
    FORGE_CHECK(threw);
    FORGE_CHECK(!std::filesystem::exists(target));
}
