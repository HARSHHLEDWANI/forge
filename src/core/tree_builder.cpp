#include "core/tree_builder.hpp"

#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#include "core/blob.hpp"
#include "core/error.hpp"
#include "core/tree.hpp"
#include "storage/repository.hpp"

namespace forge::core {

namespace {

bool is_executable(const std::filesystem::path& path) {
    // std::filesystem::perms only meaningfully reflects the POSIX
    // executable bit on POSIX filesystems. On Windows there is no
    // equivalent concept, so this always reads as non-executable there —
    // a known, platform-inherent limitation, not a Forge bug.
    std::error_code ec;
    const std::filesystem::perms perms = std::filesystem::status(path, ec).permissions();
    if (ec) {
        return false;
    }
    return (perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
}

std::string read_file_content(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ForgeError("failed to read file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

ObjectId build_tree_from_directory(
    storage::ObjectStore& store, const std::filesystem::path& directory) {
    std::vector<TreeEntry> entries;

    for (const auto& dir_entry : std::filesystem::directory_iterator(directory)) {
        const std::filesystem::path& path = dir_entry.path();
        const std::string name = path.filename().string();

        if (name == storage::kForgeDirName) {
            continue;
        }

        // is_symlink() must be checked before is_directory()/
        // is_regular_file(): those two follow symlinks by default, so a
        // symlink-to-directory would otherwise be recursed into as if it
        // were a real directory.
        if (dir_entry.is_symlink()) {
            const std::filesystem::path target = std::filesystem::read_symlink(path);
            const ObjectId blob_id = store.put_blob(Blob{target.generic_string()});
            entries.push_back(TreeEntry{name, EntryMode::Symlink, blob_id});
        } else if (dir_entry.is_directory()) {
            const ObjectId subtree_id = build_tree_from_directory(store, path);
            entries.push_back(TreeEntry{name, EntryMode::Directory, subtree_id});
        } else if (dir_entry.is_regular_file()) {
            const ObjectId blob_id = store.put_blob(Blob{read_file_content(path)});
            const EntryMode mode = is_executable(path) ? EntryMode::ExecutableFile : EntryMode::RegularFile;
            entries.push_back(TreeEntry{name, mode, blob_id});
        }
        // Other file types (sockets, FIFOs, device files) are silently
        // skipped: not meaningful content for a source-control tree.
    }

    const Tree tree(std::move(entries));
    return store.put_tree(tree);
}

namespace {

// One directory level's worth of not-yet-built entries: files ready to go
// straight into a TreeEntry, and subdirectories that still need their own
// Tree built (bottom-up) before they can become a Directory TreeEntry.
struct DirNode {
    std::vector<TreeEntry> files;
    std::map<std::string, DirNode> subdirs;
};

ObjectId build_tree_from_node(storage::ObjectStore& store, DirNode& node) {
    std::vector<TreeEntry> entries = std::move(node.files);
    for (auto& [name, child] : node.subdirs) {
        const ObjectId subtree_id = build_tree_from_node(store, child);
        entries.push_back(TreeEntry{name, EntryMode::Directory, subtree_id});
    }
    return store.put_tree(Tree(std::move(entries)));
}

} // namespace

ObjectId build_tree_from_index(storage::ObjectStore& store, const Index& index) {
    DirNode root;
    for (const IndexEntry& entry : index.entries()) {
        DirNode* current = &root;
        std::size_t start = 0;
        while (true) {
            const std::size_t slash = entry.path.find('/', start);
            if (slash == std::string::npos) {
                current->files.push_back(TreeEntry{entry.path.substr(start), entry.mode, entry.blob_id});
                break;
            }
            current = &current->subdirs[entry.path.substr(start, slash - start)];
            start = slash + 1;
        }
    }
    return build_tree_from_node(store, root);
}

} // namespace forge::core
