cmake_minimum_required(VERSION 3.25)

# B5: idasdk ships two shapes — installed (<sdk>/{include,module,...}) and
# git checkout (<sdk>/src/{include,module,...}). idasdk_init.cmake itself
# resolves both from $ENV{IDASDK}; we only have to locate the init file for
# either shape and export the (possibly git-shaped) SDK root.
if(NOT DEFINED IDASDK OR "${IDASDK}" STREQUAL "")
  set(IDASDK "$ENV{IDASDK}")
endif()
if("${IDASDK}" STREQUAL "")
  message(FATAL_ERROR "Set IDASDK (-DIDASDK=<sdk> or the IDASDK environment variable), "
                      "or configure the root project with -DWASMHEX_NATIVE=OFF.")
endif()
set(ENV{IDASDK} "${IDASDK}")
if(EXISTS "${IDASDK}/src/cmake/idasdk_init.cmake")
  include("${IDASDK}/src/cmake/idasdk_init.cmake")
else()
  include("${IDASDK}/cmake/idasdk_init.cmake")
endif()
# idasdk_init resolved the real SDK root (git checkouts: <git>/src) into
# $ENV{IDASDK}; idasdkConfig.cmake reads the plain IDASDK variable for its
# include/lib paths, so mirror the resolution here.
set(IDASDK "$ENV{IDASDK}")

# K2: verify the SDK version floor (IDA_SDK_VERSION >= 940) at configure time.
# K8: this probe runs before find_package(idasdk) so the same reading can
# default the SDK's deprecation-gate option below ahead of its option() call.
file(STRINGS "${IDASDK}/include/pro.h" _wasmhex_sdk_ver_line
     REGEX "^[ \t]*#[ \t]*define[ \t]+IDA_SDK_VERSION[ \t]+[0-9]+")
string(REGEX MATCH "[0-9]+" IDA_SDK_VERSION "${_wasmhex_sdk_ver_line}")
unset(_wasmhex_sdk_ver_line)
if(NOT IDA_SDK_VERSION OR IDA_SDK_VERSION LESS 940)
  message(FATAL_ERROR "wasmhex requires IDA SDK >= 9.4 (IDA_SDK_VERSION >= 940); "
                      "found '${IDA_SDK_VERSION}' in ${IDASDK}/include/pro.h")
endif()
message(STATUS "wasmhex: IDA_SDK_VERSION=${IDA_SDK_VERSION}")

# K8 (K4 follow-up): make the K3 deprecation gate real. The SDK's ida_add_*
# helpers append `-w` to every addon target, and clang `-w` suppresses
# -Werror=deprecated-declarations in either flag order, so K3 alone was a
# silent no-op (verified: a deprecated-marked probe built green with both
# flags on the command line). SDK >= 9.5 cmake ships IDA_DEPRECATED_AS_ERROR
# for exactly this: ON makes _idasdk_define_addon skip `-w` and adds its own
# -Werror=deprecated-declarations via the IDASDK:: interface. Default it ON
# for every 9.5+ build (CI native / ida-selfhosted / sdk-matrix and local
# presets alike); -DIDA_DEPRECATED_AS_ERROR=OFF still opts out explicitly.
# SDK 9.4 cmake has no such option: 9.4 builds stay compile-check-only for
# deprecations, kept honest by K3's 38/38 migration (grep for deprecated SDK
# APIs in sources is 0).
if(IDA_SDK_VERSION GREATER_EQUAL 950)
  set(IDA_DEPRECATED_AS_ERROR ON CACHE BOOL
      "Fail the build on deprecated SDK API use (wasmhex default: ON for SDK >= 9.5)")
  message(STATUS "wasmhex: deprecation gate live (IDA_DEPRECATED_AS_ERROR=ON, SDK >= 9.5)")
endif()

project(wasmhex LANGUAGES CXX)
find_package(idasdk REQUIRED)

# 3.11: enable_testing() must run in the STANDALONE configure for the
# format/tidy ctest entries below to land in CTestTestfile.cmake (in the
# root super-build the root already enabled it before add_subdirectory).
enable_testing()

# K4: host_support.cpp is SDK-free (pure table + matching, unit-tested by
# core_tests); host.cpp consumes it inside the processor module.
# Phase 2 branding: the processor module target/output is wasmdec
# (procs/wasmdec.dylib), the loader is wasmdecldr (loaders/wasmdecldr.dylib).
# The internal codename stays in the directory/library names (wasmhex_*).
ida_add_procmod(wasmdec SOURCES
  ida/processor/module.cpp
  ida/processor/decode.cpp
  ida/processor/emulate.cpp
  ida/processor/output.cpp
  backend/idb/metadata_schema/metadata.cpp
  backend/hexrays/register_mapping/abi.cpp
  backend/hexrays/host_adapter/host.cpp
  backend/hexrays/host_adapter/host_support.cpp
  core/toolchains/emscripten/libcxx.cpp
  core/toolchains/go/eh.cpp
  core/toolchains/go/ir.cpp
  core/toolchains/go/types.cpp
  core/toolchains/go/index.cpp
  ida/loader/options.cpp
  backend/hexrays/ctree_presentation/ctree.cpp
  backend/hexrays/ctree_presentation/rewriter_jumpout.cpp
  backend/hexrays/ctree_presentation/rewriter_shift.cpp
  backend/hexrays/ctree_presentation/rewriter_idiom.cpp
  backend/hexrays/ctree_presentation/rewriter_stack.cpp
  backend/hexrays/ctree_presentation/rewriter_eh.cpp
  backend/hexrays/ctree_presentation/rewriter_libcxx.cpp
  backend/hexrays/ctree_presentation/rewriter_copy_fuse.cpp
  backend/hexrays/ctree_presentation/rewriter_go.cpp
  backend/hexrays/ctree_presentation/ctree_float.cpp
  backend/hexrays/ctree_presentation/ctree_types.cpp
  backend/hexrays/microcode_emitter/filter.cpp
  backend/hexrays/microcode_emitter/lower_calls.cpp
  backend/hexrays/microcode_emitter/lower_control.cpp
  backend/hexrays/microcode_emitter/lower_numeric.cpp
  backend/hexrays/microcode_emitter/registers.cpp
  backend/hexrays/microcode_emitter/lower_memory.cpp
  backend/hexrays/microcode_emitter/lower_reference.cpp
  backend/hexrays/microcode_emitter/lower_exception.cpp
  backend/hexrays/microcode_emitter/lower_atomic.cpp
  backend/hexrays/microcode_emitter/lower_simd.cpp
)
# F7/F4: keep the module name distinct from the generic "wasm.dylib" (IDA's
# built-in WASM processor) and from every other built-in — wasmdec matches
# the processor short name below (DECISIONS K7).
set_target_properties(wasmdec PROPERTIES OUTPUT_NAME wasmdec)
target_link_libraries(wasmdec PRIVATE wasmhex_analysis wasmhex_debuginfo)
include(${CMAKE_CURRENT_SOURCE_DIR}/core/sources.cmake)
ida_add_loader(wasmdecldr SOURCES
  backend/hexrays/register_mapping/abi.cpp
  backend/idb/metadata_schema/metadata.cpp
  ${WASMHEX_CORE_REST}
  backend/idb/persistence/database.cpp
  ida/loader/dwarf.cpp
  ida/loader/entry.cpp
  ida/loader/options.cpp
)
# C2 S1: backend/idb/metadata_schema/metadata.cpp is shared with the
# processor module (versioned metadata accessors). It includes
# ida/processor/internal.hpp, which pulls <idaidp.hpp> from the SDK's
# module/ directory — that include path ships with the procmod target
# only, so add it here. The loader does not link hexrays, so the few
# metadata.cpp definitions that need it (decompiler-side microcode
# helpers) are compiled out below.
target_include_directories(wasmdecldr PRIVATE "${IDASDK}/module")
target_compile_definitions(wasmdecldr PRIVATE WASMHEX_NO_HEXRAYS)
target_link_libraries(wasmdecldr PRIVATE wasmhex_analysis wasmhex_debuginfo wasmhex_component)

# K3: forbid deprecated SDK APIs across the native IDA targets. On SDK >= 9.5
# this is enforced for real via IDA_DEPRECATED_AS_ERROR (defaulted ON above:
# the SDK drops its `-w` and errors on deprecated use). On SDK 9.4 these
# flags remain on the command line but are muted by the SDK's `-w` — that
# build is a compile check only (see the K8 note above).
target_compile_options(wasmdec PRIVATE -Werror=deprecated-declarations)
target_compile_options(wasmdecldr PRIVATE -Werror=deprecated-declarations)

# ---------------------------------------------------------------------------
# F2 / C6 format gate + F3 clang-tidy gate: defined in a guard so both the
# root super-build (including WASMHEX_NATIVE=OFF) and standalone
# native/wasmhex configures register the exact same targets and ctest entries.
# 3.11: the ctest entry lives INSIDE the tool guard (the root used to place
# add_test outside it — ctest red on a target that never existed when the
# tool was missing). Standalone WASMHEX_REQUIRE_TOOLS (checked at the end of
# this file) makes a missing gate tool a configure error for CI.
# ---------------------------------------------------------------------------
option(WASMHEX_REQUIRE_TOOLS
       "Fail the configure when a gate tool (clang-format/clang-tidy/libwasmtime) is missing (CI release)" OFF)
if(NOT TARGET format-check)
  find_program(WASMHEX_CLANG_FORMAT NAMES clang-format
               HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
  if(WASMHEX_CLANG_FORMAT)
    file(GLOB_RECURSE WASMHEX_FORMAT_SOURCES
         "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
    list(FILTER WASMHEX_FORMAT_SOURCES EXCLUDE REGEX "/build/")
    add_custom_target(format-check
      COMMAND ${WASMHEX_CLANG_FORMAT} --version
      COMMAND ${WASMHEX_CLANG_FORMAT} --dry-run --Werror ${WASMHEX_FORMAT_SOURCES}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "C6 format gate: clang-format --dry-run --Werror"
      VERBATIM)
    add_custom_target(format
      COMMAND ${WASMHEX_CLANG_FORMAT} -i ${WASMHEX_FORMAT_SOURCES}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
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
    # F3: fixed core-file allowlist (SDK-free binary + validation + core TUs)
    # so tidy-check is deterministic and runs in both SDK-free and full configs.
    set(WASMHEX_TIDY_CORE_SOURCES
      "${CMAKE_CURRENT_SOURCE_DIR}/core/binary/sections.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/binary/custom_sections.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/binary/instruction_stream.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/validation/validate.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/validation/function_validator.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/diagnostic.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/analysis/control_scan.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/analysis/indirect_calls.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/toolchains/go/profile.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/toolchains/go/ir.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/toolchains/go/types.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/toolchains/go/index.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/module.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/parse_code.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/binary/parse_module.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/binary/parse_code.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/binary/reader.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/debuginfo/pclntab.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/debuginfo/dwarf.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/core/component/component.cpp"
    )
    set(_wasmhex_tidy_extra_args -std=c++17 "-I${CMAKE_CURRENT_SOURCE_DIR}")
    if(APPLE)
      if(CMAKE_OSX_SYSROOT)
        list(APPEND _wasmhex_tidy_extra_args -isysroot "${CMAKE_OSX_SYSROOT}")
      else()
        execute_process(COMMAND xcrun --show-sdk-path
                        OUTPUT_VARIABLE _wasmhex_apple_sysroot
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
        if(_wasmhex_apple_sysroot)
          list(APPEND _wasmhex_tidy_extra_args -isysroot "${_wasmhex_apple_sysroot}")
        endif()
        unset(_wasmhex_apple_sysroot)
      endif()
    endif()
    add_custom_target(tidy-check
      COMMAND ${WASMHEX_CLANG_TIDY} --version
      COMMAND ${WASMHEX_CLANG_TIDY}
              "--config-file=${CMAKE_CURRENT_SOURCE_DIR}/../../.clang-tidy"
              ${WASMHEX_TIDY_CORE_SOURCES}
              -- ${_wasmhex_tidy_extra_args}
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
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
# B5 install: the IDAUSR layout (procs/wasmdec.dylib + loaders/wasmdecldr.dylib).
# Use with: cmake --install build/native --prefix <dir>   (or --preset install-user)
# ---------------------------------------------------------------------------
install(TARGETS wasmdec DESTINATION procs)
install(TARGETS wasmdecldr DESTINATION loaders)

# M6-S2 (P2): when native/wasmhex is the top-level CMake source directory
# (e.g. build/rebuild-20261107), wire tests/runner so `wasmdec_runner` builds
# directly in this tree.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/runner/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../tests/runner"
                   "${CMAKE_CURRENT_BINARY_DIR}/runner")
endif()

# ---------------------------------------------------------------------------
# 3.11 / V4-V5: standalone flavor of the root's WASMHEX_REQUIRE_TOOLS check.
# Covers what THIS configure probes: clang-format + clang-tidy (gates above)
# and libwasmtime (tests/runner, wired standalone above). wat2wasm is checked
# by the root super-build, which is where tests/fixtures is configured.
# ---------------------------------------------------------------------------
if(WASMHEX_REQUIRE_TOOLS AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_wasmhex_missing_tools "")
  if(NOT WASMHEX_CLANG_FORMAT)
    list(APPEND _wasmhex_missing_tools "clang-format")
  endif()
  if(NOT WASMHEX_CLANG_TIDY)
    list(APPEND _wasmhex_missing_tools "clang-tidy")
  endif()
  if(NOT WASMTIME_LIBRARY)
    list(APPEND _wasmhex_missing_tools "libwasmtime")
  endif()
  if(_wasmhex_missing_tools)
    string(REPLACE ";" ", " _wasmhex_missing_list "${_wasmhex_missing_tools}")
    message(FATAL_ERROR
            "WASMHEX_REQUIRE_TOOLS=ON but missing: ${_wasmhex_missing_list}. "
            "Configure from the repo root to also check wat2wasm, or set "
            "-DWASMHEX_REQUIRE_TOOLS=OFF.")
  endif()
  message(STATUS "wasmhex: WASMHEX_REQUIRE_TOOLS satisfied (clang-format, clang-tidy, libwasmtime)")
endif()
