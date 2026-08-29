#pragma once

#include <filesystem>

#include "core/index.hpp"

namespace forge::storage {

// Wraps the single ".forge/index" file. Not content-addressed like
// ObjectStore (the index lives at a fixed path and mutates), so it's a
// distinct, much smaller storage abstraction — matching architecture.md's
// separate ObjectStore/IndexStore bullets.
class IndexStore {
public:
    explicit IndexStore(std::filesystem::path index_path);

    // A fresh repository has no index file yet; that's not corruption,
    // just an empty index.
    core::Index load() const;

    // Atomically persists `index` via write_file_atomic.
    void save(const core::Index& index) const;

private:
    std::filesystem::path index_path_;
};

} // namespace forge::storage
