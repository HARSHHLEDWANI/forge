#include "storage/object_store.hpp"

#include <fstream>
#include <sstream>

#include "core/error.hpp"
#include "core/object_encoding.hpp"
#include "storage/atomic_file.hpp"

namespace forge::storage {

ObjectStore::ObjectStore(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path ObjectStore::path_for(const core::ObjectId& id) const {
    const std::string hex = id.to_hex();
    return root_ / hex.substr(0, 2) / hex.substr(2);
}

core::ObjectId ObjectStore::put(std::string_view type, std::string_view payload) {
    const std::string canonical = core::encode_canonical_object(type, payload);
    const core::ObjectId id = core::ObjectId::of(canonical);
    const std::filesystem::path path = path_for(id);

    if (!std::filesystem::exists(path)) {
        std::filesystem::create_directories(path.parent_path());
        write_file_atomic(path, canonical);
    }

    return id;
}

bool ObjectStore::contains(const core::ObjectId& id) const {
    return std::filesystem::exists(path_for(id));
}

StoredObject ObjectStore::get(const core::ObjectId& id) const {
    const std::filesystem::path path = path_for(id);

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw core::ForgeError("object not found: " + id.to_hex());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string canonical = buffer.str();

    // Corruption detection: re-derive the id from the bytes actually on
    // disk and compare against what the caller asked for.
    if (core::ObjectId::of(canonical) != id) {
        throw core::ForgeError("object corrupted: " + id.to_hex());
    }

    const std::optional<core::DecodedObject> decoded = core::decode_canonical_object(canonical);
    if (!decoded) {
        throw core::ForgeError("object malformed: " + id.to_hex());
    }

    return StoredObject{decoded->type, decoded->payload};
}

core::ObjectId ObjectStore::put_blob(const core::Blob& blob) {
    return put("blob", blob.content);
}

core::Blob ObjectStore::get_blob(const core::ObjectId& id) const {
    const StoredObject stored = get(id);
    if (stored.type != "blob") {
        throw core::ForgeError("object is not a blob: " + id.to_hex());
    }
    return core::Blob{stored.payload};
}

} // namespace forge::storage
