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

add_library(forge_core STATIC
  src/core/logging.cpp
  src/core/hashing.cpp
  src/core/object_id.cpp
  src/core/object_encoding.cpp
  src/core/tree.cpp
  src/core/tree_builder.cpp
  src/cli/cli.cpp
  src/storage/safe_path.cpp
  src/storage/atomic_file.cpp
  src/storage/repository.cpp
  src/storage/object_store.cpp
)
target_include_directories(forge_core PUBLIC src)
target_link_libraries(forge_core PRIVATE OpenSSL::Crypto)
forge_apply_warnings(forge_core)

add_executable(forge src/main.cpp)
target_link_libraries(forge PRIVATE forge_core)
forge_apply_warnings(forge)

enable_testing()
add_subdirectory(tests)
