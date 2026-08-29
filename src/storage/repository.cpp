#include "storage/repository.hpp"

#include <sstream>
#include <system_error>

#include "storage/atomic_file.hpp"

namespace forge::storage {

std::optional<std::filesystem::path> discover_repository_root(const std::filesystem::path& start) {
    std::error_code ec;
    std::filesystem::path current = std::filesystem::canonical(start, ec);
    if (ec) {
        return std::nullopt;
    }

    while (true) {
        std::error_code is_dir_ec;
        if (std::filesystem::is_directory(current / kForgeDirName, is_dir_ec) && !is_dir_ec) {
            return current;
        }

        const std::filesystem::path parent = current.parent_path();
        if (parent == current) {
            return std::nullopt; // reached filesystem root without finding one
        }
        current = parent;
    }
}

InitResult initialize_repository(const std::filesystem::path& target_dir) {
    std::filesystem::create_directories(target_dir);

    const std::filesystem::path forge_dir = target_dir / kForgeDirName;
    const bool reinitialized = std::filesystem::is_directory(forge_dir);

    std::filesystem::create_directories(forge_dir);

    const std::filesystem::path config_path = forge_dir / kConfigFileName;
    if (!std::filesystem::exists(config_path)) {
        // "storage_root" makes the configurable object-storage location
        // explicit and human-editable from day one, even though nothing
        // reads it back yet — the object store lands in a later phase.
        const std::filesystem::path storage_root =
            std::filesystem::absolute(forge_dir / "objects").lexically_normal();
        std::ostringstream config;
        config << "format_version = 1\n";
        config << "storage_root = " << storage_root.generic_string() << '\n';
        write_file_atomic(config_path, config.str());
    }

    return InitResult{forge_dir, reinitialized};
}

} // namespace forge::storage
