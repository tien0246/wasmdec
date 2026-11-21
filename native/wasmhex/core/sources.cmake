# M2/T2: SDK-free static libraries per module. The loader build, the core
# unit tests and the fuzz targets all include this file, so a new core file
# is still added once. Dependency direction (enforced by the
# wasmhex_dep_check target): wasmhex_binary <- wasmhex_validation <- rest.
set(WASMHEX_CORE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Files not yet moved to their own modules (M3: parse_code Go scans ->
# toolchains/go, module.cpp SP-global -> analysis, pclntab -> debuginfo).
# Declared before the target guard: every includer sees the list even when
# the libraries already exist.
set(WASMHEX_CORE_REST
  "${WASMHEX_CORE_DIR}/toolchains/go/profile.cpp"
  "${WASMHEX_CORE_DIR}/toolchains/emscripten/libcxx.cpp"
  "${WASMHEX_CORE_DIR}/toolchains/emscripten/libcxx_build.cpp"
  "${WASMHEX_CORE_DIR}/toolchains/go/eh.cpp"
  "${WASMHEX_CORE_DIR}/toolchains/go/eh_build.cpp"
  "${WASMHEX_CORE_DIR}/toolchains/go/ir.cpp"
  "${WASMHEX_CORE_DIR}/toolchains/go/types.cpp"
  "${WASMHEX_CORE_DIR}/toolchains/go/index.cpp"
  "${WASMHEX_CORE_DIR}/module.cpp"
  "${WASMHEX_CORE_DIR}/parse_code.cpp"
  "${WASMHEX_CORE_DIR}/binary/parse_module.cpp"
  "${WASMHEX_CORE_DIR}/binary/parse_code.cpp"
  "${WASMHEX_CORE_DIR}/diagnostic.cpp"
)

# The wasmhex static libraries are linked into IDA addon shared objects
# (wasmdecldr.so / .dylib). GNU ld rejects non-PIC objects in a shared
# library (R_X86_64_PC32 relocations) — Apple toolchains are PIC by
# default, which hid this until the first Linux CI run. Every includer of
# this file gets PIC libs, whichever scope defines the targets first.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT TARGET wasmhex_binary) # guard: the root project includes this twice

add_library(wasmhex_binary STATIC
  "${WASMHEX_CORE_DIR}/binary/sections.cpp"
  "${WASMHEX_CORE_DIR}/binary/custom_sections.cpp"
  "${WASMHEX_CORE_DIR}/binary/instruction_stream.cpp"
  "${WASMHEX_CORE_DIR}/binary/reader.cpp"
)
target_include_directories(wasmhex_binary PUBLIC "${WASMHEX_CORE_DIR}/..")
target_compile_features(wasmhex_binary PUBLIC cxx_std_17)

add_library(wasmhex_validation STATIC
  "${WASMHEX_CORE_DIR}/validation/validate.cpp"
  "${WASMHEX_CORE_DIR}/validation/function_validator.cpp"
)
target_include_directories(wasmhex_validation PUBLIC "${WASMHEX_CORE_DIR}/..")
target_compile_features(wasmhex_validation PUBLIC cxx_std_17)
target_link_libraries(wasmhex_validation PUBLIC wasmhex_binary)

# M5 (§9.1): the analysis module — per-body scans over the decoded module
# (control/stack/dispatch analysis in control_scan.cpp — one TU for now; the
# cfg/constants/c_stack/indirect_calls split needs the SPLIT_PLAN refactor,
# honestly declined inside a moves-only round) and element/table slot
# resolution feeding indirect-call lowering (indirect_calls.cpp). The pure
# dataflow framework lives beside it as analysis/ssa.hpp.
add_library(wasmhex_analysis STATIC
  "${WASMHEX_CORE_DIR}/analysis/control_scan.cpp"
  "${WASMHEX_CORE_DIR}/analysis/indirect_calls.cpp"
  "${WASMHEX_CORE_DIR}/analysis/mem_drefs.cpp"
  "${WASMHEX_CORE_DIR}/analysis/param_pointers.cpp"
)
target_include_directories(wasmhex_analysis PUBLIC "${WASMHEX_CORE_DIR}/..")
target_compile_features(wasmhex_analysis PUBLIC cxx_std_17)
target_link_libraries(wasmhex_analysis PUBLIC wasmhex_validation)

# M5 (§9.1): the debuginfo module — Go pclntab recovery and the SDK-free
# DWARF decoder. The IDA-facing DWARF entry (external_debug_info qf* IO +
# msg() diagnostics + the fill-empty-first merge) stays in loader/dwarf.cpp
# and is compiled into the loader addon, not this library.
add_library(wasmhex_debuginfo STATIC
  "${WASMHEX_CORE_DIR}/debuginfo/pclntab.cpp"
  "${WASMHEX_CORE_DIR}/debuginfo/dwarf.cpp"
)
target_include_directories(wasmhex_debuginfo PUBLIC "${WASMHEX_CORE_DIR}/..")
target_compile_features(wasmhex_debuginfo PUBLIC cxx_std_17)
target_link_libraries(wasmhex_debuginfo PUBLIC wasmhex_binary)

# M5 (§9.1): the component-model container module — the layer-order walk
# that collects embedded core modules (C7). Consumed by the loader entry.
add_library(wasmhex_component STATIC
  "${WASMHEX_CORE_DIR}/component/component.cpp"
)
target_include_directories(wasmhex_component PUBLIC "${WASMHEX_CORE_DIR}/..")
target_compile_features(wasmhex_component PUBLIC cxx_std_17)
target_link_libraries(wasmhex_component PUBLIC wasmhex_binary)

# M2/T2 + M6-S1: assert the one-directional, SDK-free layout at build time.
add_custom_target(wasmhex_dep_check
  COMMAND ${CMAKE_COMMAND}
          "-DBUILD_DIR=${CMAKE_BINARY_DIR}"
          -P "${CMAKE_CURRENT_LIST_DIR}/../../../tools/dep_check.cmake"
  COMMENT "wasmhex dependency-graph gate"
  VERBATIM)
endif() # target guard
