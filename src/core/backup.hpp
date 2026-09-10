#pragma once

#include <cstddef>
#include <filesystem>

#include "core/verify.hpp"
#include "storage/object_store.hpp"
#include "storage/ref_store.hpp"
#include "storage/repository.hpp"

namespace forge::core {

struct BackupResult {
    std::size_t objects_copied;
    std::size_t objects_already_present;
};

// Backs up a repository into `backup_dir`, shaped exactly like a bare
// Forge repository's `.forge` directory (objects/, refs/heads/<name>,
// HEAD — the same shape server/repo_registry.hpp's hosted repos have),
// so verify_backup() below is just core::verify_repository() applied to
// it, and restore_backup() is just core::clone()'s own object-copying
// and ref-recreation logic pointed at a local source instead of a
// network one.
//
// Copies whatever object the backup doesn't already have — an
// incremental, resumable, rsync-style backup: safe to run repeatedly
// against the same destination, since an already-present object is
// detected (ObjectStore::contains) and skipped rather than re-copied.
// Every branch ref and HEAD are only updated after every object they
// depend on has been copied, so an interrupted backup never leaves
// `backup_dir` pointing at a ref its own object store can't actually
// satisfy — the worst an interruption does is leave some refs stale
// (the previous backup's state) until create_backup() is run again to
// completion. Throws core::ForgeError on any I/O failure.
BackupResult create_backup(
    const storage::ObjectStore& source_objects, const storage::RefStore& source_refs,
    const std::filesystem::path& backup_dir);

// A read-only integrity check of a backup made by create_backup() —
// literally core::verify_repository() (Phase 11) pointed at
// `backup_dir`'s object store and refs, since that's all a backup is.
VerifyReport verify_backup(const std::filesystem::path& backup_dir);

// Restores `backup_dir` into a fresh repository at `target_dir`:
// initialize_repository, copy every reachable object, check out
// whichever branch HEAD names (before any branch ref exists, exactly
// like core::clone() — see core/remote.cpp's doc comment on why that
// ordering matters), then recreate every other branch ref. Throws
// core::ForgeError if the backup is missing, malformed, or `target_dir`
// isn't suitable for a fresh repository.
storage::InitResult restore_backup(const std::filesystem::path& backup_dir, const std::filesystem::path& target_dir);

} // namespace forge::core
