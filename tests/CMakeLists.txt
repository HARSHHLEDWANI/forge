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
)
target_include_directories(forge_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(forge_unit_tests PRIVATE forge_core)
forge_apply_warnings(forge_unit_tests)

add_test(NAME forge_unit_tests COMMAND forge_unit_tests)
