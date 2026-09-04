#include <fstream>

#include "core/blob.hpp"
#include "core/diff.hpp"
#include "core/ignore_rules.hpp"
#include "core/index.hpp"
#include "core/object_id.hpp"
#include "storage/object_store.hpp"
#include "storage/repository.hpp"
#include "support/temp_dir.hpp"
#include "support/test_framework.hpp"

using forge::core::BlobDiff;
using forge::core::ChangeType;
using forge::core::diff_blob_content;
using forge::core::diff_index;
using forge::core::diff_working_tree;
using forge::core::EntryChange;
using forge::core::EntryMode;
using forge::core::FileDiff;
using forge::core::IgnoreRules;
using forge::core::Index;
using forge::core::IndexEntry;
using forge::core::LineOp;
using forge::core::ObjectId;
using forge::core::render_unified_diff;
using forge::core::snapshot_working_tree;
using forge::storage::ObjectStore;
using forge::test::TempDir;

namespace {

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::vector<std::string> texts_with_op(const BlobDiff& diff, LineOp op) {
    std::vector<std::string> result;
    for (const auto& line : diff.lines) {
        if (line.op == op) {
            result.push_back(line.text);
        }
    }
    return result;
}

} // namespace

FORGE_TEST_CASE(diff_index_identical_states_yields_no_changes) {
    Index a;
    a.upsert(IndexEntry{"x.txt", EntryMode::RegularFile, ObjectId::of("x")});
    Index b;
    b.upsert(IndexEntry{"x.txt", EntryMode::RegularFile, ObjectId::of("x")});

    FORGE_CHECK(diff_index(a, b).empty());
}

FORGE_TEST_CASE(diff_index_reports_added_path) {
    Index old_state;
    Index new_state;
    new_state.upsert(IndexEntry{"new.txt", EntryMode::RegularFile, ObjectId::of("n")});

    const auto changes = diff_index(old_state, new_state);
    FORGE_CHECK(changes.size() == 1);
    FORGE_CHECK(changes.at(0).type == ChangeType::Added);
    FORGE_CHECK(!changes.at(0).old_blob_id.has_value());
    FORGE_CHECK(changes.at(0).new_blob_id == ObjectId::of("n"));
}

FORGE_TEST_CASE(diff_index_reports_deleted_path) {
    Index old_state;
    old_state.upsert(IndexEntry{"gone.txt", EntryMode::RegularFile, ObjectId::of("g")});
    Index new_state;

    const auto changes = diff_index(old_state, new_state);
    FORGE_CHECK(changes.size() == 1);
    FORGE_CHECK(changes.at(0).type == ChangeType::Deleted);
    FORGE_CHECK(changes.at(0).old_blob_id == ObjectId::of("g"));
    FORGE_CHECK(!changes.at(0).new_blob_id.has_value());
}

FORGE_TEST_CASE(diff_index_reports_modified_content) {
    Index old_state;
    old_state.upsert(IndexEntry{"m.txt", EntryMode::RegularFile, ObjectId::of("v1")});
    Index new_state;
    new_state.upsert(IndexEntry{"m.txt", EntryMode::RegularFile, ObjectId::of("v2")});

    const auto changes = diff_index(old_state, new_state);
    FORGE_CHECK(changes.size() == 1);
    FORGE_CHECK(changes.at(0).type == ChangeType::Modified);
}

FORGE_TEST_CASE(diff_index_reports_mode_only_change_as_modified) {
    Index old_state;
    old_state.upsert(IndexEntry{"m.txt", EntryMode::RegularFile, ObjectId::of("v1")});
    Index new_state;
    new_state.upsert(IndexEntry{"m.txt", EntryMode::ExecutableFile, ObjectId::of("v1")});

    const auto changes = diff_index(old_state, new_state);
    FORGE_CHECK(changes.size() == 1);
    FORGE_CHECK(changes.at(0).type == ChangeType::Modified);
}

FORGE_TEST_CASE(diff_index_sorts_results_by_path) {
    Index old_state;
    Index new_state;
    new_state.upsert(IndexEntry{"zebra.txt", EntryMode::RegularFile, ObjectId::of("z")});
    new_state.upsert(IndexEntry{"apple.txt", EntryMode::RegularFile, ObjectId::of("a")});

    const auto changes = diff_index(old_state, new_state);
    FORGE_CHECK(changes.size() == 2);
    FORGE_CHECK(changes.at(0).path == "apple.txt");
    FORGE_CHECK(changes.at(1).path == "zebra.txt");
}

FORGE_TEST_CASE(snapshot_working_tree_hashes_files_without_a_store) {
    TempDir dir;
    write_file(dir.path() / "a.txt", "hello");

    const Index snapshot = snapshot_working_tree(IgnoreRules::parse(""), dir.path());
    FORGE_CHECK(snapshot.entries().size() == 1);
    FORGE_CHECK(snapshot.find("a.txt")->mode == EntryMode::RegularFile);
}

FORGE_TEST_CASE(snapshot_working_tree_excludes_forge_metadata_dir) {
    TempDir dir;
    forge::storage::initialize_repository(dir.path());
    write_file(dir.path() / "tracked.txt", "content");

    const Index snapshot = snapshot_working_tree(IgnoreRules::parse(""), dir.path());
    FORGE_CHECK(snapshot.entries().size() == 1);
    FORGE_CHECK(snapshot.find("tracked.txt").has_value());
}

FORGE_TEST_CASE(snapshot_working_tree_respects_ignore_rules) {
    TempDir dir;
    write_file(dir.path() / "keep.txt", "keep");
    write_file(dir.path() / "skip.log", "skip");

    const Index snapshot = snapshot_working_tree(IgnoreRules::parse("*.log\n"), dir.path());
    FORGE_CHECK(snapshot.entries().size() == 1);
    FORGE_CHECK(snapshot.find("keep.txt").has_value());
}

FORGE_TEST_CASE(diff_blob_content_identical_has_no_insert_or_delete) {
    const BlobDiff diff = diff_blob_content("a\nb\nc\n", "a\nb\nc\n");
    FORGE_CHECK(!diff.binary);
    FORGE_CHECK(texts_with_op(diff, LineOp::Insert).empty());
    FORGE_CHECK(texts_with_op(diff, LineOp::Delete).empty());
}

FORGE_TEST_CASE(diff_blob_content_detects_pure_insertion) {
    const BlobDiff diff = diff_blob_content("a\nb\n", "a\nx\nb\n");
    FORGE_CHECK(texts_with_op(diff, LineOp::Delete).empty());
    FORGE_CHECK(texts_with_op(diff, LineOp::Insert) == std::vector<std::string>{"x"});
    FORGE_CHECK(texts_with_op(diff, LineOp::Context) == (std::vector<std::string>{"a", "b"}));
}

FORGE_TEST_CASE(diff_blob_content_detects_pure_deletion) {
    const BlobDiff diff = diff_blob_content("a\nb\nc\n", "a\nc\n");
    FORGE_CHECK(texts_with_op(diff, LineOp::Insert).empty());
    FORGE_CHECK(texts_with_op(diff, LineOp::Delete) == std::vector<std::string>{"b"});
}

FORGE_TEST_CASE(diff_blob_content_treats_full_line_replacement_as_delete_then_insert) {
    const BlobDiff diff = diff_blob_content("a\n", "b\n");
    FORGE_CHECK(diff.lines.size() == 2);
    FORGE_CHECK(diff.lines.at(0).op == LineOp::Delete);
    FORGE_CHECK(diff.lines.at(0).text == "a");
    FORGE_CHECK(diff.lines.at(1).op == LineOp::Insert);
    FORGE_CHECK(diff.lines.at(1).text == "b");
}

FORGE_TEST_CASE(diff_blob_content_edit_script_reconstructs_both_sides) {
    const std::string old_content = "one\ntwo\nthree\nfour\n";
    const std::string new_content = "zero\none\nthree\nfive\nfour\n";
    const BlobDiff diff = diff_blob_content(old_content, new_content);

    std::string reconstructed_old;
    std::string reconstructed_new;
    for (const auto& line : diff.lines) {
        if (line.op != LineOp::Insert) {
            reconstructed_old += line.text + "\n";
        }
        if (line.op != LineOp::Delete) {
            reconstructed_new += line.text + "\n";
        }
    }
    FORGE_CHECK(reconstructed_old == old_content);
    FORGE_CHECK(reconstructed_new == new_content);
}

FORGE_TEST_CASE(diff_blob_content_detects_binary_via_embedded_nul) {
    const std::string binary_content("a\0b", 3);
    const BlobDiff diff = diff_blob_content(binary_content, "text");
    FORGE_CHECK(diff.binary);
    FORGE_CHECK(diff.lines.empty());
}

FORGE_TEST_CASE(render_unified_diff_reports_binary_files) {
    const BlobDiff diff{true, {}};
    const std::string rendered = render_unified_diff("a.bin", diff);
    FORGE_CHECK(rendered == "Binary files a/a.bin and b/a.bin differ\n");
}

FORGE_TEST_CASE(render_unified_diff_is_empty_for_no_changes) {
    const BlobDiff diff{false, {{LineOp::Context, "same"}}};
    FORGE_CHECK(render_unified_diff("a.txt", diff).empty());
}

FORGE_TEST_CASE(render_unified_diff_shows_hunk_header_and_prefixed_lines) {
    const BlobDiff diff = diff_blob_content("a\nb\n", "a\nx\nb\n");
    const std::string rendered = render_unified_diff("f.txt", diff);
    FORGE_CHECK(rendered.find("--- a/f.txt\n") == 0);
    FORGE_CHECK(rendered.find("+++ b/f.txt\n") != std::string::npos);
    FORGE_CHECK(rendered.find("@@ -1,2 +1,3 @@\n") != std::string::npos);
    FORGE_CHECK(rendered.find(" a\n") != std::string::npos);
    FORGE_CHECK(rendered.find("+x\n") != std::string::npos);
    FORGE_CHECK(rendered.find(" b\n") != std::string::npos);
}

FORGE_TEST_CASE(diff_working_tree_reports_unstaged_modification) {
    TempDir repo;
    TempDir objects_dir;
    ObjectStore objects(objects_dir.path());
    const auto blob_id = objects.put_blob(forge::core::Blob{"old content"});

    Index index;
    index.upsert(IndexEntry{"a.txt", EntryMode::RegularFile, blob_id});
    write_file(repo.path() / "a.txt", "new content");

    const auto diffs = diff_working_tree(objects, index, IgnoreRules::parse(""), repo.path());
    FORGE_CHECK(diffs.size() == 1);
    FORGE_CHECK(diffs.at(0).path == "a.txt");
    FORGE_CHECK(diffs.at(0).type == ChangeType::Modified);
}

FORGE_TEST_CASE(diff_working_tree_reports_deletion) {
    TempDir repo;
    TempDir objects_dir;
    ObjectStore objects(objects_dir.path());
    const auto blob_id = objects.put_blob(forge::core::Blob{"content\n"});

    Index index;
    index.upsert(IndexEntry{"a.txt", EntryMode::RegularFile, blob_id});
    // a.txt deliberately absent from disk: an unstaged deletion.

    const auto diffs = diff_working_tree(objects, index, IgnoreRules::parse(""), repo.path());
    FORGE_CHECK(diffs.size() == 1);
    FORGE_CHECK(diffs.at(0).type == ChangeType::Deleted);
    FORGE_CHECK(texts_with_op(diffs.at(0).content, LineOp::Delete) == std::vector<std::string>{"content"});
}

FORGE_TEST_CASE(diff_working_tree_excludes_untracked_files) {
    TempDir repo;
    TempDir objects_dir;
    ObjectStore objects(objects_dir.path());
    write_file(repo.path() / "untracked.txt", "not tracked");

    const auto diffs = diff_working_tree(objects, Index{}, IgnoreRules::parse(""), repo.path());
    FORGE_CHECK(diffs.empty());
}

FORGE_TEST_CASE(diff_working_tree_excludes_unchanged_files) {
    TempDir repo;
    TempDir objects_dir;
    ObjectStore objects(objects_dir.path());
    const auto blob_id = objects.put_blob(forge::core::Blob{"same"});

    Index index;
    index.upsert(IndexEntry{"a.txt", EntryMode::RegularFile, blob_id});
    write_file(repo.path() / "a.txt", "same");

    const auto diffs = diff_working_tree(objects, index, IgnoreRules::parse(""), repo.path());
    FORGE_CHECK(diffs.empty());
}
