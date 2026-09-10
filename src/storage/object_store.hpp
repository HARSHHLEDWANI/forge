#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/blob.hpp"
#include "core/commit.hpp"
#include "core/object_id.hpp"
#include "core/tree.hpp"

namespace forge::storage {

struct StoredObject {
    std::string type;
    std::string payload;
};

struct ObjectStoreScan {
    std::vector<core::ObjectId> object_ids;
    // Anything physically present under the store's root that isn't a
    // validly-shaped object path (two-hex-char shard directory holding a
    // 62-hex-char file). In practice this can only be a write_file_atomic
    // temp file (see atomic_file.hpp) orphaned by a process that crashed
    // between creating it and renaming it into place — the store never
    // contains anything else by construction.
    std::vector<std::filesystem::path> unexpected_files;
};

// Content-addressed loose-object store: one file per object, laid out as
// objects/<id[0:2]>/<id[2:]>, exactly as documented in storage.md.
//
// Known limitation: put()/get() hold the full canonical object bytes in
// memory. Fine for the common case (source-sized blobs), but true
// streaming for very large files is deferred to Phase 5 (add), where a
// real caller with real file-size/mtime context exists to design the
// streaming API against, rather than a speculative one now.
class ObjectStore {
public:
    explicit ObjectStore(std::filesystem::path root);

    // Encodes (type, payload) canonically, hashes it, and writes it as a
    // loose object if not already present. Objects are immutable and
    // content-addressed, so an existing file at the computed path is
    // always already exactly this content — writing is simply skipped.
    // That's the store's whole duplicate-detection story: it falls out
    // of content addressing rather than needing a separate check.
    core::ObjectId put(std::string_view type, std::string_view payload);

    // Reads a loose object back and verifies it by re-deriving its
    // ObjectId from the bytes actually on disk. Throws core::ForgeError
    // if the object is missing, unreadable, or corrupted (hash mismatch
    // or a malformed canonical encoding).
    StoredObject get(const core::ObjectId& id) const;

    bool contains(const core::ObjectId& id) const;

    // Blob-typed convenience over put()/get(): get_blob rejects fetching
    // a non-blob object as a blob, which raw get() cannot.
    core::ObjectId put_blob(const core::Blob& blob);
    core::Blob get_blob(const core::ObjectId& id) const;

    // Tree-typed convenience: rejects fetching a non-tree object as a
    // tree, and surfaces a malformed tree payload as ForgeError instead
    // of leaving decode_tree's nullopt for the caller to handle.
    core::ObjectId put_tree(const core::Tree& tree);
    core::Tree get_tree(const core::ObjectId& id) const;

    // Commit-typed convenience: rejects fetching a non-commit object as a
    // commit, and surfaces a malformed commit payload as ForgeError
    // instead of leaving decode_commit's nullopt for the caller to handle.
    core::ObjectId put_commit(const core::Commit& commit);
    core::Commit get_commit(const core::ObjectId& id) const;

    // Enumerates everything physically present in the store, classifying
    // each entry as a well-formed object id or an unexpected file. Used
    // by core::verify_repository (Phase 11) — not part of normal
    // read/write operation, so it's the one method here that walks the
    // whole store rather than addressing a single object.
    ObjectStoreScan scan() const;

private:
    std::filesystem::path root_;
    std::filesystem::path path_for(const core::ObjectId& id) const;
};

} // namespace forge::storage
