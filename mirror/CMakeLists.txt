# Native 64-bit portable mirror target (ADR 0001).
#
# Compiles the GENERATED mirror sources (mirror/src/**), not the decomp directly.
# Generate them first:
#     python3 tools/mirror/build_mirror.py --gen-only
# then configure/build this as an ordinary out-of-tree CMake project, e.g.:
#     cmake -S mirror -B mirror/build-cmake && cmake --build mirror/build-cmake
#
# This is deliberately separate from the repo-root CMakeLists.txt (the Windows
# 32-bit game_engine build). This target is 64-bit and toolchain-portable; it grows
# as tools/mirror/build_manifest.txt gains TUs.
cmake_minimum_required(VERSION 3.16)
project(stairfax_mirror C)

set(CMAKE_C_STANDARD 11)

# Generated sources live under src/ (gitignored). Glob is fine here: the source of
# truth is the manifest + generator, and CMake is re-run after each generation.
file(GLOB_RECURSE MIRROR_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

if(NOT MIRROR_SRCS)
  message(FATAL_ERROR
    "No mirror sources found. Run: python3 tools/mirror/build_mirror.py --gen-only")
endif()

# Hand-written portable runtime for the mirror target (OS-globals accessor, ...).
file(GLOB MIRROR_RUNTIME_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.c")

add_library(stairfax_mirror OBJECT ${MIRROR_SRCS} ${MIRROR_RUNTIME_SRCS})

set(DECOMP "${CMAKE_CURRENT_SOURCE_DIR}/../decomp")
target_include_directories(stairfax_mirror PRIVATE
  "${DECOMP}/include" "${DECOMP}" "${DECOMP}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime")

# 64-bit, portable. Warnings are silenced for now: the decomp is not warning-clean
# under clang/gcc and that is tracked separately from correctness (the diff harness).
target_compile_options(stairfax_mirror PRIVATE -m64 -w)
