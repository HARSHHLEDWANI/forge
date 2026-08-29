add_executable(forge_unit_tests
  unit/main.cpp
  unit/cli_tests.cpp
  unit/logging_tests.cpp
)
target_include_directories(forge_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(forge_unit_tests PRIVATE forge_core)
forge_apply_warnings(forge_unit_tests)

add_test(NAME forge_unit_tests COMMAND forge_unit_tests)
