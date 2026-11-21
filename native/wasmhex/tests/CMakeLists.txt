cmake_minimum_required(VERSION 3.16)
# Standalone unit tests for the pure core/ of wasmhex (C2): no IDA SDK is
# needed, so this configures and builds with any plain C++17 compiler.
project(wasmhex_core_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../core/sources.cmake)

add_executable(core_tests
  main.cpp
  test_leb.cpp
  test_parser_validator.cpp
  test_control_scan.cpp
  test_value_type.cpp
  test_opcodes.cpp
  test_validate.cpp
  test_dataflow.cpp
  test_function_validator.cpp
  test_spec_oracle.cpp
  test_go_profile.cpp
  test_go_types.cpp
  test_go_index.cpp
  test_instruction_stream.cpp
  test_host_support.cpp
  test_mem_drefs.cpp
  test_param_pointers.cpp
  ../backend/hexrays/host_adapter/host_support.cpp
  ${WASMHEX_CORE_REST}
)
target_link_libraries(core_tests PRIVATE wasmhex_analysis wasmhex_debuginfo)
target_include_directories(core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_options(core_tests PRIVATE -Wall -Wextra)

# Fixture root for the parser/validator oracle tests (tests/golden + tests/fixtures).
target_compile_definitions(core_tests PRIVATE
  WASMHEX_REPO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../..")

# Public-release corpus gate (corpus_guard.hpp): default OFF = corpus-absent
# test cases skip-with-notice; ON (root option WASMHEX_REQUIRE_CORPUS, set by
# sweep machines / release CI) makes the absence a test failure.
if(NOT DEFINED WASMHEX_REQUIRE_CORPUS)
  option(WASMHEX_REQUIRE_CORPUS
         "Fail corpus-dependent tests when wasm-test/ corpora are missing instead of skipping with a notice" OFF)
endif()
if(WASMHEX_REQUIRE_CORPUS)
  target_compile_definitions(core_tests PRIVATE WASMHEX_REQUIRE_CORPUS_TESTS=1)
endif()

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
