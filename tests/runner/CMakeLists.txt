cmake_minimum_required(VERSION 3.25)

# M6-S2 (P2): C++ idalib F5 runner (dump-golden / expect / f5).
#
# Can be added via add_subdirectory() from the root super-build, from
# native/wasmhex/CMakeLists.txt (when native/wasmhex is the top-level
# source dir, e.g. build/rebuild-20261107), or configured standalone.
#
# Runtime library resolution note (macOS):
#   Linking uses the SDK import stubs (idasdk::idalib -> libidalib.dylib +
#   libida.dylib in <idasdk>/src/lib/arm64_mac_64), whose install_name is
#   `@rpath/libida.dylib` and `@rpath/libidalib.dylib`.
#   At runtime, `@rpath` MUST resolve to the real 35 MB libida.dylib inside
#   the IDA application bundle, NEVER the SDK stub directory (loading the
#   stub at runtime causes dyld initializer segfaults). The bundle dir
#   defaults to the newest /IDA*.app found at the root of the volume (or
#   the IDA_BUNDLE_MACOS_DIR environment variable); override with
#   -DWASMHEX_IDA_BUNDLE_MACOS_DIR=<dir> if IDA is elsewhere.

if(NOT DEFINED WASMHEX_IDA_BUNDLE_MACOS_DIR OR "${WASMHEX_IDA_BUNDLE_MACOS_DIR}" STREQUAL "")
  set(WASMHEX_IDA_BUNDLE_MACOS_DIR "" CACHE PATH
      "IDA.app/Contents/MacOS holding the real libida.dylib/libidalib.dylib (default: newest /IDA*.app, or env IDA_BUNDLE_MACOS_DIR)")
  if(NOT WASMHEX_IDA_BUNDLE_MACOS_DIR)
    if(DEFINED ENV{IDA_BUNDLE_MACOS_DIR} AND NOT "$ENV{IDA_BUNDLE_MACOS_DIR}" STREQUAL "")
      set(WASMHEX_IDA_BUNDLE_MACOS_DIR "$ENV{IDA_BUNDLE_MACOS_DIR}" CACHE PATH "" FORCE)
    else()
      file(GLOB _wasmhex_ida_bundle_dirs "/*/IDA*.app/Contents/MacOS")
      if(_wasmhex_ida_bundle_dirs)
        list(SORT _wasmhex_ida_bundle_dirs)
        list(GET _wasmhex_ida_bundle_dirs -1 _wasmhex_ida_bundle_default)
        set(WASMHEX_IDA_BUNDLE_MACOS_DIR "${_wasmhex_ida_bundle_default}" CACHE PATH "" FORCE)
      endif()
      unset(_wasmhex_ida_bundle_dirs)
      unset(_wasmhex_ida_bundle_default)
    endif()
  endif()
endif()
if(WASMHEX_IDA_BUNDLE_MACOS_DIR)
  message(STATUS "IDA bundle (rpath): ${WASMHEX_IDA_BUNDLE_MACOS_DIR}")
else()
  message(STATUS "IDA bundle not found: set -DWASMHEX_IDA_BUNDLE_MACOS_DIR=<IDA.app/Contents/MacOS> for runtime rpath")
endif()

if(NOT TARGET idasdk::idalib)
  if(NOT DEFINED IDASDK OR "${IDASDK}" STREQUAL "")
    set(IDASDK "$ENV{IDASDK}")
  endif()
  if("${IDASDK}" STREQUAL "")
    message(FATAL_ERROR "wasmdec_runner requires IDASDK (-DIDASDK=<sdk> or env IDASDK)")
  endif()
  set(ENV{IDASDK} "${IDASDK}")
  if(EXISTS "${IDASDK}/src/cmake/idasdk_init.cmake")
    include("${IDASDK}/src/cmake/idasdk_init.cmake")
  else()
    include("${IDASDK}/cmake/idasdk_init.cmake")
  endif()
  set(IDASDK "$ENV{IDASDK}")
  project(wasmdec_runner LANGUAGES CXX)
  find_package(idasdk REQUIRED)
endif()

add_executable(wasmdec_runner
  wasmhex_runner.cpp
  wasmtime_eval.cpp
)

find_path(WASMTIME_INCLUDE_DIR NAMES wasmtime.h HINTS /opt/homebrew/include /usr/local/include)
find_library(WASMTIME_LIBRARY NAMES wasmtime HINTS /opt/homebrew/lib /usr/local/lib)

target_link_libraries(wasmdec_runner PRIVATE idasdk::idalib)
target_compile_definitions(wasmdec_runner PRIVATE USE_STANDARD_FILE_FUNCTIONS=1)
if(WASMTIME_INCLUDE_DIR AND WASMTIME_LIBRARY)
  target_compile_definitions(wasmdec_runner PRIVATE WASMHEX_HAVE_WASMTIME=1)
  target_include_directories(wasmdec_runner PRIVATE "${WASMTIME_INCLUDE_DIR}")
  target_link_libraries(wasmdec_runner PRIVATE "${WASMTIME_LIBRARY}")
endif()

# Place the built binary in the top-level build directory so
# `cmake --build <build-dir> --target wasmdec_runner` produces
# `<build-dir>/wasmdec_runner` directly.
set_target_properties(wasmdec_runner PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  BUILD_WITH_INSTALL_RPATH TRUE
  INSTALL_RPATH "${WASMHEX_IDA_BUNDLE_MACOS_DIR}"
  BUILD_RPATH "${WASMHEX_IDA_BUNDLE_MACOS_DIR}"
)

# When configured from native/wasmhex (e.g. build/rebuild-20261107), also expose
# runtime_oracle and diffrt targets in the same build tree.
if(NOT TARGET runtime_oracle AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../runtime_oracle/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../runtime_oracle"
                   "${CMAKE_BINARY_DIR}/tests/runtime_oracle")
endif()
if(NOT TARGET diffrt_transform AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../diffrt/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../diffrt"
                   "${CMAKE_BINARY_DIR}/tests/diffrt")
endif()

# ---------------------------------------------------------------------------
# BUG A regression gate: copy_fuse must guard op == cit_expr before reading
# cinsn_t::cexpr. Before the fix the block scan read it->cexpr->op /
# pure_var_copy(next->cexpr) unguarded - through a cit_goto statement that
# puns a 4-byte cgoto_t into a cexpr_t and reads e->op 4 bytes past the
# allocation (ASan heap-buffer-overflow READ, deterministic on gowasm-balls
# at function 4, f5 and dump-golden alike). It slipped through every
# existing gate because none ran the runner under ASan; this target is the
# closing piece of that gap. Requires the WASMHEX_ASAN=ON configuration
# (preset macos-asan) so the wasmdec dylib the runner loads is itself
# instrumented. The gate installs the instrumented dylibs into a private
# prefix and runs `f5 <module> --limit 20` against them - 20 functions is
# comfortably past the first trigger.
# ---------------------------------------------------------------------------
if(WASMHEX_ASAN)
  if(NOT TARGET wasmdec OR NOT TARGET wasmdecldr)
    message(FATAL_ERROR
      "asan-smoke needs the instrumented wasmdec dylibs: configure the root "
      "super-build with WASMHEX_ASAN=ON (cmake --preset macos-asan), not a "
      "standalone tests/runner tree")
  endif()
  set(WASMHEX_ASAN_SMOKE_MODULE "" CACHE PATH
      "wasm module for the asan-smoke f5 gate (default: wasm-test/gowasm-balls/main.wasm when present, else the in-repo loop-transfers fixture)")
  set(_wasmhex_asan_smoke_module "${WASMHEX_ASAN_SMOKE_MODULE}")
  if(_wasmhex_asan_smoke_module STREQUAL "")
    foreach(_wasmhex_asan_cand
            "${CMAKE_CURRENT_SOURCE_DIR}/../../wasm-test/gowasm-balls/main.wasm"
            "${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/wasmhex_loop_transfers.wasm")
      if(EXISTS "${_wasmhex_asan_cand}")
        set(_wasmhex_asan_smoke_module "${_wasmhex_asan_cand}")
        break()
      endif()
    endforeach()
  endif()
  if(NOT EXISTS "${_wasmhex_asan_smoke_module}")
    message(FATAL_ERROR "asan-smoke: no smoke module found - set "
                        "-DWASMHEX_ASAN_SMOKE_MODULE=<module.wasm>")
  endif()
  message(STATUS "wasmdec_runner: asan-smoke module = ${_wasmhex_asan_smoke_module}")
  add_custom_target(asan-smoke
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_BINARY_DIR}/asan-install"
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}"
            --prefix "${CMAKE_BINARY_DIR}/asan-install"
    COMMAND /usr/bin/env ASAN_OPTIONS=detect_leaks=0
            "${CMAKE_BINARY_DIR}/wasmdec_runner" f5 "${_wasmhex_asan_smoke_module}"
            --limit 20 --idausr "${CMAKE_BINARY_DIR}/asan-install"
    DEPENDS wasmdec_runner wasmdec wasmdecldr
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "asan-smoke: runner f5 --limit 20 under AddressSanitizer"
    VERBATIM)
  add_test(NAME asan-smoke
           COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target asan-smoke)
endif()
