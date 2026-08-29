#include "storage/index_store.hpp"

#include <fstream>
#include <sstream>

#include "core/error.hpp"
#include "storage/atomic_file.hpp"

namespace forge::storage {

IndexStore::IndexStore(std::filesystem::path index_path) : index_path_(std::move(index_path)) {}

core::Index IndexStore::load() const {
    if (!std::filesystem::exists(index_path_)) {
        return core::Index{};
    }

    std::ifstream in(index_path_, std::ios::binary);
    if (!in) {
        throw core::ForgeError("failed to open index: " + index_path_.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    const std::optional<core::Index> decoded = core::decode_index(buffer.str());
    if (!decoded) {
        throw core::ForgeError("index is corrupted: " + index_path_.string());
    }
    return *decoded;
}

void IndexStore::save(const core::Index& index) const {
    write_file_atomic(index_path_, index.encode());
}

} // namespace forge::storage
