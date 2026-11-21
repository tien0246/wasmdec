cmake_minimum_required(VERSION 3.16)

# B5: regenerate the committed .wasm fixtures whenever their .wat source is
# newer (wat2wasm). Outputs are written in place over the committed binaries
# — safe because the command only reruns when the .wat changed.
#
# Feature flags are probed once at configure time: 273 fixtures build with
# default flags, ~36 need --enable-all (GC/typed-refs syntax), and the rest
# were produced by toolchains newer than the local wat2wasm — those are
# skipped (the committed binary is the source of truth; see gen/README.md).
project(wasmhex_fixtures NONE)

# K2: the C++ generator tool does not need wat2wasm — wire it before the
# wat2wasm early-return below so `fixtures-gen` is always available.
add_subdirectory(gen)

find_program(WAT2WASM wat2wasm)
if(NOT WAT2WASM)
  message(WARNING "wat2wasm not found: fixture regeneration target disabled")
  return()
endif()

set(FIXTURE_DIRS "${CMAKE_CURRENT_LIST_DIR}" "${CMAKE_CURRENT_LIST_DIR}/brtable")
set(WAT_SOURCES)
foreach(dir IN LISTS FIXTURE_DIRS)
  file(GLOB wats "${dir}/*.wat")
  list(APPEND WAT_SOURCES ${wats})
endforeach()

set(FIXTURE_OUTPUTS)
set(SKIPPED 0)
foreach(wat IN LISTS WAT_SOURCES)
  get_filename_component(name "${wat}" NAME_WLE)
  get_filename_component(dir "${wat}" DIRECTORY)
  set(out "${dir}/${name}.wasm")
  if(NOT EXISTS "${out}")
    continue() # gen-script fixtures own their outputs
  endif()
  set(flags "")
  execute_process(
    COMMAND "${WAT2WASM}" "${wat}" -o "${CMAKE_BINARY_DIR}/probe.wasm"
    OUTPUT_QUIET ERROR_QUIET
    RESULT_VARIABLE default_result)
  if(NOT default_result EQUAL 0)
    execute_process(
      COMMAND "${WAT2WASM}" --enable-all "${wat}" -o "${CMAKE_BINARY_DIR}/probe.wasm"
      OUTPUT_QUIET ERROR_QUIET
      RESULT_VARIABLE all_result)
    if(all_result EQUAL 0)
      set(flags "--enable-all")
    else()
      math(EXPR SKIPPED "${SKIPPED} + 1")
      continue()
    endif()
  endif()
  # Stamp-based regeneration: a fresh checkout makes every .wat look
  # "newer" than its .wasm by mtime, but wat2wasm versions encode a few
  # modules slightly differently — only regenerate when the .wat is truly
  # newer than the committed .wasm, tracked through a stamp in the build
  # tree so the target stays idempotent.
  set(stamp "${CMAKE_BINARY_DIR}/stamps/${name}.stamp")
  add_custom_command(
    OUTPUT "${stamp}"
    COMMAND ${CMAKE_COMMAND}
      -DWAT="${WAT2WASM}" -DWAT_FILE="${wat}" -DWASM_FILE="${out}"
      -DFLAGS="${flags}" -DSTAMP="${stamp}"
      -P "${CMAKE_CURRENT_LIST_DIR}/regen_wasm.cmake"
    DEPENDS "${wat}"
    COMMENT "fixture ${name} (regenerates only if .wat is newer)"
    VERBATIM)
  list(APPEND FIXTURE_OUTPUTS "${stamp}")
endforeach()
file(REMOVE "${CMAKE_BINARY_DIR}/probe.wasm")

message(STATUS "fixtures: ${FIXTURE_OUTPUTS} regenerable, ${SKIPPED} skipped (newer-toolchain binaries)")
add_custom_target(fixtures DEPENDS ${FIXTURE_OUTPUTS})
