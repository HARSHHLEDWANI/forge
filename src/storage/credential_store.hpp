#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace forge::storage {

// Persists bearer tokens issued by POST /login (server/app.cpp) keyed
// by remote URL, so `forge push`/`fetch`/`clone` can authenticate
// without the user re-typing a password on every command — the same
// role git's own credential.helper plays, minus the pluggable backends
// V1 doesn't need. One flat file, `~/.forge/credentials`, plaintext,
// one "<remote_url> <token>" line per remote: the same convention git
// itself uses for `~/.git-credentials` (also plaintext) rather than
// something this project would need to invent and justify.
class CredentialStore {
public:
    // `home_dir`, if given, overrides where `.forge/credentials` is
    // read/written — for tests; a real CLI invocation omits it and gets
    // resolve_home_dir()'s platform-appropriate default.
    explicit CredentialStore(std::optional<std::filesystem::path> home_dir = std::nullopt);

    void save(const std::string& remote_url, const std::string& token) const;
    std::optional<std::string> find(const std::string& remote_url) const;
    void remove(const std::string& remote_url) const;

    std::filesystem::path credentials_path() const;

private:
    std::filesystem::path home_dir_;
};

// The current user's home directory: $HOME on POSIX, else
// %USERPROFILE% on Windows. Throws core::ForgeError if neither is set —
// there's no sane fallback location to silently pick instead.
std::filesystem::path resolve_home_dir();

} // namespace forge::storage
