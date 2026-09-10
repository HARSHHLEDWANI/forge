#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"

namespace forge::core {

struct VerifyReport {
    std::size_t objects_checked = 0;
    // "<hex id>: <reason>" — an object physically in the store whose
    // content hash doesn't match its filename, or whose canonical
    // envelope doesn't parse. Found by re-reading every object the store
    // actually contains, independent of whether anything still
    // references it.
    std::vector<std::string> corrupt_objects;
    // "<hex id> (<what> referenced by <branch '<name>'|detached HEAD>)" —
    // an id some reachable commit, tree, or blob points at that isn't in
    // the store at all. Found by walking every branch's full history
    // (and detached HEAD's, if set) — the flat per-object scan above
    // can't detect an absence by only looking at what's present.
    std::vector<std::string> missing_objects;
    // Anything under the object store that isn't a validly-shaped object
    // (see storage::ObjectStoreScan) — in practice only ever a
    // write_file_atomic temp file orphaned by an interrupted write (see
    // atomic_file.hpp). Harmless clutter, not a correctness problem:
    // reported so it can be spotted and cleaned up, but excluded from
    // ok().
    std::vector<std::filesystem::path> orphaned_files;

    bool ok() const { return corrupt_objects.empty() && missing_objects.empty(); }
};

// A read-only integrity check ("fsck"): verifies every object physically
// in `objects`, then walks the full reachable history from every branch
// in `refs` and from detached HEAD (an unborn branch is not an error —
// see ref_store.hpp) to confirm every tree, blob, and parent commit a
// live ref depends on actually resolves. Never modifies the repository.
//
// The same object reachable from more than one branch is only checked
// once and, if broken, reported once — fixing it fixes it for every
// branch that depends on it, so attributing the same problem to each of
// them would just be noise.
VerifyReport verify_repository(const storage::ObjectStore& objects, const storage::RefStore& refs);

} // namespace forge::core
