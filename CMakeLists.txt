cmake_minimum_required(VERSION 3.25)
project(forge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug)
endif()

function(forge_apply_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(PostgreSQL REQUIRED)

add_library(forge_core STATIC
  src/core/logging.cpp
  src/core/hashing.cpp
  src/core/object_id.cpp
  src/core/object_encoding.cpp
  src/core/tree.cpp
  src/core/tree_builder.cpp
  src/core/ignore_rules.cpp
  src/core/index.cpp
  src/core/staging.cpp
  src/core/commit.cpp
  src/core/committing.cpp
  src/core/branching.cpp
  src/core/log.cpp
  src/core/checkout.cpp
  src/core/diff.cpp
  src/core/merge.cpp
  src/core/verify.cpp
  src/core/remote_protocol.cpp
  src/core/remote.cpp
  src/core/base64.cpp
  src/cli/cli.cpp
  src/storage/safe_path.cpp
  src/storage/atomic_file.cpp
  src/storage/repository.cpp
  src/storage/object_store.cpp
  src/storage/index_store.cpp
  src/storage/ref_store.cpp
  src/storage/repo_lock.cpp
  src/storage/auth_store.cpp
  src/transport/socket_compat.cpp
  src/transport/http_message.cpp
  src/transport/http_parser.cpp
  src/transport/http_server.cpp
  src/transport/http_client.cpp
  src/server/app.cpp
  src/server/repo_registry.cpp
  src/domain/password.cpp
  src/domain/ssh_key.cpp
  src/domain/token.cpp
  src/domain/permissions.cpp
  src/domain/audit_log.cpp
  src/database/postgres_connection.cpp
  src/database/migrations.cpp
  src/database/repository_directory.cpp
)
target_include_directories(forge_core PUBLIC src)
target_link_libraries(forge_core PRIVATE OpenSSL::Crypto PostgreSQL::PostgreSQL)
if(WIN32)
  target_link_libraries(forge_core PRIVATE ws2_32)
endif()
forge_apply_warnings(forge_core)

add_executable(forge src/main.cpp)
target_link_libraries(forge PRIVATE forge_core)
forge_apply_warnings(forge)

add_executable(forge-server src/main_server.cpp)
target_link_libraries(forge-server PRIVATE forge_core)
forge_apply_warnings(forge-server)

enable_testing()
add_subdirectory(tests)
