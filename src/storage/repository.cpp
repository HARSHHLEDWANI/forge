#include "storage/repository.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

#include "core/error.hpp"
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

namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return text;
}

} // namespace

RepositoryConfig load_config(const std::filesystem::path& forge_dir) {
    const std::filesystem::path config_path = forge_dir / kConfigFileName;

    std::ifstream in(config_path, std::ios::binary);
    if (!in) {
        throw core::ForgeError("repository config not found: " + config_path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string content = buffer.str();

    RepositoryConfig config;
    bool has_storage_root = false;

    std::size_t pos = 0;
    while (pos < content.size()) {
        std::size_t line_end = content.find('\n', pos);
        if (line_end == std::string::npos) {
            line_end = content.size();
        }
        const std::string_view line = trim(std::string_view(content).substr(pos, line_end - pos));
        pos = line_end + 1;

        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos) {
            continue;
        }
        const std::string_view key = trim(line.substr(0, eq));
        const std::string_view value = trim(line.substr(eq + 1));

        if (key == "format_version") {
            try {
                config.format_version = std::stoi(std::string(value));
            } catch (const std::exception&) {
                throw core::ForgeError("repository config has malformed format_version: " + config_path.string());
            }
        } else if (key == "storage_root") {
            config.storage_root = value;
            has_storage_root = true;
        }
        // Unknown keys are ignored for forward compatibility.
    }

    if (!has_storage_root) {
        throw core::ForgeError("repository config missing storage_root: " + config_path.string());
    }
    return config;
}

} // namespace forge::storage
