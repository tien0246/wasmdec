cmake_minimum_required(VERSION 3.25)

# B5 super-build: one root configure for the whole C++ toolchain — the IDA
# modules (optional, needs the SDK), the pure core unit tests, the C1 golden
# runner and the fixture regeneration. Presets in CMakePresets.json select
# the combination; WASMHEX_NATIVE=OFF keeps a configure SDK-free.
project(wasmhex_super NONE)

option(WASMHEX_NATIVE "Build wasmdec + wasmdecldr (requires IDASDK)" ON)
option(WASMHEX_FUZZ "Build the fuzz targets in tests/fuzz" OFF)
# 3.11 / V4-V5: release CI sets this to ON so a missing optional-gate tool
# (clang-format, clang-tidy, wat2wasm, libwasmtime) is a CONFIGURE error
# instead of a silently disabled gate. Default OFF keeps local SDK-free
# configures working without the tools; the check runs at the end of this
# file, after every find_program/find_library has had its chance.
option(WASMHEX_REQUIRE_TOOLS
       "Fail the configure when clang-format/clang-tidy/wat2wasm/libwasmtime are missing (CI release)" OFF)
# BUG A regression (copy_fuse cit_expr union guard): instrument the whole
# tree - crucially the wasmhex procs dylib, where the rewriters live - so
# ASan sees OOB reads inside the module the runner loads, not just in the
# runner binary. The runner's asan-smoke target/ctest is registered only
# in this configuration (preset macos-asan).
option(WASMHEX_ASAN "Build the native modules and the runner with AddressSanitizer (asan-smoke gate)" OFF)
# Public-release corpus gate: wasm-test/ is git-ignored, so public clones have
# no corpora. Default OFF = corpus-check + corpus-dependent core_tests cases
# skip-with-notice (one line: how to fetch, tests/CORPUS.md). Sweep machines /
# release CI set ON to make a missing corpus FAIL everywhere it is probed.
option(WASMHEX_REQUIRE_CORPUS
       "Fail when the git-ignored wasm-test/ corpora are missing instead of skipping with a notice (sweep machines / release CI)" OFF)

if(WASMHEX_ASAN)
  # Same flags the investigation build used (build/investigate-sigsegv-asan):
  # global CXX flags so every target - wasmhex dylibs, loader, runner -
  # carries the instrumentation and links the ASan runtime.
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address)
endif()

enable_testing()

if(WASMHEX_NATIVE)
  add_subdirectory(native/wasmhex)
endif()
add_subdirectory(native/wasmhex/tests)
add_subdirectory(native/wasmhex/tests/runner)
add_subdirectory(tests/fixtures)
# M6-S3: C++ runtime oracle + diffrt tools (libwasmtime resolved from brew path;
# CI story comes in Slice 4). tests/runner below re-adds both directories,
# guarded on the same targets, when it is configured standalone from
# native/wasmhex — so the root adds each one exactly once BEFORE the runner
# and the runner's NOT TARGET guards skip (the sources.cmake target-guard
# pattern). WASMHEX_SUPER_BUILD lets tests/runtime_oracle reserve the target
# name even when libwasmtime is missing, so the runner's guard still skips.
set(WASMHEX_SUPER_BUILD ON)
if(NOT TARGET runtime_oracle)
  add_subdirectory(tests/runtime_oracle)
endif()
if(NOT TARGET diffrt_transform)
  add_subdirectory(tests/diffrt)
endif()
if(WASMHEX_NATIVE)
  add_subdirectory(tests/runner)
endif()
if(WASMHEX_FUZZ)
  add_subdirectory(tests/fuzz)
endif()

# ---------------------------------------------------------------------------
# V2 (todo §10) / public-release: corpus manifest check (WASMHEX_REQUIRE_CORPUS
# is the option declared with the other options at the top of this file).
# The large sweep corpora live under the git-ignored wasm-test/ tree, so
# their presence + integrity are pinned by tests/corpus/manifest.tsv and
# enforced here. Public-clones have no wasm-test/ at all: by default the
# check then SKIPS-with-notice (green ctest + one line telling how to fetch
# the corpora) instead of failing an outsider's build. Sweep machines /
# release CI set WASMHEX_REQUIRE_CORPUS=ON to restore the strict behavior:
# a missing or corrupted corpus then FAILS (never a vacuous pass — the
# script also refuses an empty manifest, and a PRESENT file with the wrong
# size/SHA always fails even in skip mode). Plain CMake, SDK-free, runs in
# every configure.
# ---------------------------------------------------------------------------
if(NOT TARGET corpus-check)
  add_test(NAME corpus-check
           COMMAND ${CMAKE_COMMAND}
                   -D WASMHEX_CORPUS_MANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus/manifest.tsv
                   -D WASMHEX_REQUIRE_CORPUS=${WASMHEX_REQUIRE_CORPUS}
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus/check_corpus.cmake)
endif()

# ---------------------------------------------------------------------------
# F2 / C6 format gate + F3 clang-tidy gate at the root super-build level so
# they run in SDK-free configurations (WASMHEX_NATIVE=OFF) as well as full
# native builds. Guarded by NOT TARGET when WASMHEX_NATIVE=ON already
# registered them in native/wasmhex/CMakeLists.txt.
# 3.11: the ctest entry lives INSIDE the tool guard — a configure without
# clang-format registers neither the target nor the test (before, add_test
# sat outside the guard and ctest went red calling a target that never
# existed). Release CI makes the missing tool itself a configure error via
# WASMHEX_REQUIRE_TOOLS (checked at the end of this file).
# ---------------------------------------------------------------------------
if(NOT TARGET format-check)
  find_program(WASMHEX_CLANG_FORMAT NAMES clang-format
               HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
  if(WASMHEX_CLANG_FORMAT)
    # V5: the gate scans BOTH trees — native/wasmhex/** and the top-level
    # tests/ + tools/ C++ (runner, runtime_oracle, diffrt, fuzz, gen_fixtures;
    # the glob is suffix-filtered so .wat/.md/.txt stay out). The tool is
    # pinned de-facto by the HINTS order: Homebrew llvm (clang-format 23.x)
    # wins over any system clang-format.
    file(GLOB_RECURSE WASMHEX_FORMAT_SOURCES
         "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/*.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/*.hpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.hpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cc"
         "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
         "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.inc"
         "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cc"
         "${CMAKE_CURRENT_SOURCE_DIR}/tools/*.cpp")
    list(FILTER WASMHEX_FORMAT_SOURCES EXCLUDE REGEX "/build/")
    add_custom_target(format-check
      COMMAND ${WASMHEX_CLANG_FORMAT} --version
      COMMAND ${WASMHEX_CLANG_FORMAT} --dry-run --Werror ${WASMHEX_FORMAT_SOURCES}
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex"
      COMMENT "C6 format gate: clang-format --dry-run --Werror"
      VERBATIM)
    add_custom_target(format
      COMMAND ${WASMHEX_CLANG_FORMAT} -i ${WASMHEX_FORMAT_SOURCES}
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex"
      COMMENT "C6: rewrite sources with clang-format"
      VERBATIM)
    add_test(NAME format-check
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target format-check)
  else()
    message(STATUS "clang-format not found: the format/format-check targets and the "
                   "format-check test are unavailable")
  endif()
endif()

if(NOT TARGET tidy-check)
  find_program(WASMHEX_CLANG_TIDY NAMES clang-tidy
               HINTS /opt/homebrew/opt/llvm/bin /opt/homebrew/opt/llvm@19/bin /usr/local/opt/llvm/bin)
  if(WASMHEX_CLANG_TIDY)
    set(WASMHEX_TIDY_CORE_SOURCES
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/binary/sections.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/binary/custom_sections.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/binary/instruction_stream.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/validation/validate.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/validation/function_validator.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/diagnostic.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/analysis/control_scan.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/analysis/indirect_calls.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/toolchains/go/profile.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/toolchains/go/ir.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/module.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/parse_code.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/binary/parse_module.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/binary/parse_code.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/binary/reader.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/debuginfo/pclntab.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/debuginfo/dwarf.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex/core/component/component.cpp"
    )
    set(_wasmhex_tidy_extra_args -std=c++17 "-I${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex")
    if(APPLE)
      execute_process(COMMAND xcrun --show-sdk-path
                      OUTPUT_VARIABLE _wasmhex_apple_sysroot
                      OUTPUT_STRIP_TRAILING_WHITESPACE
                      ERROR_QUIET)
      if(_wasmhex_apple_sysroot)
        list(APPEND _wasmhex_tidy_extra_args -isysroot "${_wasmhex_apple_sysroot}")
      endif()
      unset(_wasmhex_apple_sysroot)
    endif()
    add_custom_target(tidy-check
      COMMAND ${WASMHEX_CLANG_TIDY} --version
      COMMAND ${WASMHEX_CLANG_TIDY}
              "--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy"
              ${WASMHEX_TIDY_CORE_SOURCES}
              -- ${_wasmhex_tidy_extra_args}
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/native/wasmhex"
      COMMENT "F3 clang-tidy gate over wasmhex core sources"
      VERBATIM)
    unset(_wasmhex_tidy_extra_args)
    add_test(NAME tidy-check
             COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target tidy-check)
  else()
    message(STATUS "clang-tidy not found: the tidy-check target is unavailable")
  endif()
endif()

# ---------------------------------------------------------------------------
# 3.11 / V4 / V5: WASMHEX_REQUIRE_TOOLS=ON turns every silently-skipped
# optional gate into a configure failure. Covers the tools this configure
# actually probes: clang-format + clang-tidy (format/tidy gates above),
# wat2wasm (tests/fixtures regeneration), libwasmtime (runtime_oracle,
# diffrt, the idalib runner). CI release jobs set -DWASMHEX_REQUIRE_TOOLS=ON.
# ---------------------------------------------------------------------------
if(WASMHEX_REQUIRE_TOOLS)
  set(_wasmhex_missing_tools "")
  if(NOT WASMHEX_CLANG_FORMAT)
    list(APPEND _wasmhex_missing_tools "clang-format")
  endif()
  if(NOT WASMHEX_CLANG_TIDY)
    list(APPEND _wasmhex_missing_tools "clang-tidy")
  endif()
  if(NOT WAT2WASM)
    list(APPEND _wasmhex_missing_tools "wat2wasm")
  endif()
  if(NOT WASMTIME_LIBRARY)
    list(APPEND _wasmhex_missing_tools "libwasmtime")
  endif()
  if(_wasmhex_missing_tools)
    string(REPLACE ";" ", " _wasmhex_missing_list "${_wasmhex_missing_tools}")
    message(FATAL_ERROR
            "WASMHEX_REQUIRE_TOOLS=ON but missing: ${_wasmhex_missing_list}. "
            "Install the tools (brew install llvm wabt wasmtime / apt equivalents) "
            "or reconfigure with -DWASMHEX_REQUIRE_TOOLS=OFF.")
  endif()
  message(STATUS "wasmhex: WASMHEX_REQUIRE_TOOLS satisfied (clang-format, clang-tidy, wat2wasm, libwasmtime)")
endif()
