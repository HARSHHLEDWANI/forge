#include "storage/credential_store.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

#include "core/error.hpp"
#include "storage/atomic_file.hpp"

namespace forge::storage {

namespace {

// Splits "<remote_url> <token>" the same way, and for the same
// reason, storage/auth_store.cpp's split_fields does: the remote URL
// never contains a space (core::parse_remote_url's "forge://host:port/
// repo-name" grammar has no room for one), so a single split on the
// first space is unambiguous and the token — the remainder of the
// line — never needs to itself be free of spaces.
std::optional<std::pair<std::string, std::string>> split_line(const std::string& line) {
    const std::size_t space = line.find(' ');
    if (space == std::string::npos) {
        return std::nullopt;
    }
    return std::make_pair(line.substr(0, space), line.substr(space + 1));
}

} // namespace

std::filesystem::path resolve_home_dir() {
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return home;
    }
    if (const char* user_profile = std::getenv("USERPROFILE"); user_profile != nullptr && *user_profile != '\0') {
        return user_profile;
    }
    throw core::ForgeError("could not determine the current user's home directory (HOME/USERPROFILE not set)");
}

CredentialStore::CredentialStore(std::optional<std::filesystem::path> home_dir)
    : home_dir_(home_dir ? std::move(*home_dir) : resolve_home_dir()) {}

std::filesystem::path CredentialStore::credentials_path() const { return home_dir_ / ".forge" / "credentials"; }

void CredentialStore::save(const std::string& remote_url, const std::string& token) const {
    std::vector<std::pair<std::string, std::string>> entries;
    std::ifstream in(credentials_path(), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        const std::optional<std::pair<std::string, std::string>> parsed = split_line(line);
        if (parsed && parsed->first != remote_url) {
            entries.push_back(*parsed);
        }
    }
    entries.emplace_back(remote_url, token);

    std::filesystem::create_directories(credentials_path().parent_path());
    std::ostringstream out;
    for (const auto& [url, saved_token] : entries) {
        out << url << ' ' << saved_token << '\n';
    }
    write_file_atomic(credentials_path(), out.str());
}

std::optional<std::string> CredentialStore::find(const std::string& remote_url) const {
    std::ifstream in(credentials_path(), std::ios::binary);
    if (!in) {
        return std::nullopt; // no credentials file yet: nothing saved, not corruption
    }
    std::string line;
    while (std::getline(in, line)) {
        const std::optional<std::pair<std::string, std::string>> parsed = split_line(line);
        if (parsed && parsed->first == remote_url) {
            return parsed->second;
        }
    }
    return std::nullopt;
}

void CredentialStore::remove(const std::string& remote_url) const {
    std::vector<std::pair<std::string, std::string>> entries;
    std::ifstream in(credentials_path(), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        const std::optional<std::pair<std::string, std::string>> parsed = split_line(line);
        if (parsed && parsed->first != remote_url) {
            entries.push_back(*parsed);
        }
    }

    std::ostringstream out;
    for (const auto& [url, saved_token] : entries) {
        out << url << ' ' << saved_token << '\n';
    }
    write_file_atomic(credentials_path(), out.str());
}

} // namespace forge::storage
