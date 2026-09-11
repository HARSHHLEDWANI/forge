add_executable(forge_unit_tests
  unit/main.cpp
  unit/cli_tests.cpp
  unit/logging_tests.cpp
  unit/safe_path_tests.cpp
  unit/atomic_file_tests.cpp
  unit/repository_tests.cpp
  unit/hashing_tests.cpp
  unit/object_id_tests.cpp
  unit/object_encoding_tests.cpp
  unit/object_store_tests.cpp
  unit/tree_tests.cpp
  unit/tree_builder_tests.cpp
  unit/ignore_rules_tests.cpp
  unit/index_tests.cpp
  unit/index_store_tests.cpp
  unit/staging_tests.cpp
  unit/commit_tests.cpp
  unit/ref_store_tests.cpp
  unit/committing_tests.cpp
  unit/branching_tests.cpp
  unit/log_tests.cpp
  unit/checkout_tests.cpp
  unit/diff_tests.cpp
  unit/merge_tests.cpp
  unit/repo_lock_tests.cpp
  unit/verify_tests.cpp
  unit/http_message_tests.cpp
  unit/http_parser_tests.cpp
  unit/http_server_tests.cpp
  unit/rate_limiter_tests.cpp
  unit/credential_store_tests.cpp
  unit/remote_protocol_tests.cpp
  unit/repo_registry_tests.cpp
  unit/remote_tests.cpp
  unit/base64_tests.cpp
  unit/password_tests.cpp
  unit/ssh_key_tests.cpp
  unit/token_tests.cpp
  unit/permissions_tests.cpp
  unit/audit_log_tests.cpp
  unit/auth_store_tests.cpp
  unit/server_auth_tests.cpp
  unit/postgres_connection_tests.cpp
  unit/migrations_tests.cpp
  unit/repository_directory_tests.cpp
  unit/collaboration_directory_tests.cpp
  unit/merge_in_object_store_tests.cpp
  unit/server_collaboration_tests.cpp
  unit/worker_pool_tests.cpp
  unit/backup_tests.cpp
)
target_include_directories(forge_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(forge_unit_tests PRIVATE forge_core Threads::Threads)
if(WIN32)
  target_link_libraries(forge_unit_tests PRIVATE ws2_32)
endif()
target_compile_definitions(forge_unit_tests PRIVATE FORGE_MIGRATIONS_DIR="${CMAKE_SOURCE_DIR}/migrations")
forge_apply_warnings(forge_unit_tests)

add_test(NAME forge_unit_tests COMMAND forge_unit_tests)
