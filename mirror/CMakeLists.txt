# Native 64-bit portable mirror target (ADR 0001).
#
# Compiles the GENERATED mirror sources (mirror/src/**), not the decomp directly.
# Generate them first:
#     python3 tools/mirror/build_mirror.py --gen-only
# then configure/build this as an ordinary out-of-tree CMake project, e.g.:
#     CC=clang cmake -S mirror -B mirror/build-cmake && cmake --build mirror/build-cmake
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
# mirror/include is the complete generated header tree; decomp/include is NOT on
# the path (a quoted include would otherwise find untransformed originals).
target_include_directories(stairfax_mirror BEFORE PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"   # COMPLETE generated tree (decomp + port shadows + rules)
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
  "${DECOMP}" "${DECOMP}/src")

# 64-bit, portable. Warnings are silenced for now: the decomp is not warning-clean
# under clang/gcc and that is tracked separately from correctness (the diff harness).
# The decomp uses MWCC __declspec(weak / section) and clang accepts it with
# -fdeclspec; gcc has no equivalent, so the mirror target currently requires clang
# (configure with CC=clang). Keep in sync with tools/mirror/build_mirror.py CFLAGS.
if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "stairfax_mirror requires clang (MWCC __declspec needs -fdeclspec). "
                      "Configure with: CC=clang cmake -S mirror -B mirror/build-cmake")
endif()
# Implicit function declarations are allowed as MWCC did. The safety check (fail
# when an implicitly-declared callee really returns a pointer, which would truncate
# to int) runs in tools/mirror/build_mirror.py, which gates these sources before
# CMake compiles them.
target_compile_options(stairfax_mirror PRIVATE -m64 -w -fdeclspec
                       -Wno-error=implicit-function-declaration)
