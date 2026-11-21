# K2 phase-1: the C++ port of the fixture generators (replaces the six
# make_*.py scripts). Explicit target: cmake --build <fixtures-build> --target
# fixtures-gen. Every subcommand is idempotent (content-identical files are
# not rewritten; clang-based groups also honor mtime like the python did).
project(wasmhex_gen_fixtures CXX)
cmake_minimum_required(VERSION 3.16)
add_executable(gen_fixtures gen_fixtures.cpp)
target_compile_features(gen_fixtures PRIVATE cxx_std_17)

set(GEN_DIR ${CMAKE_CURRENT_LIST_DIR})
set(FIXTURES_DIR ${GEN_DIR}/..)
add_custom_target(
  fixtures-gen
  COMMAND $<TARGET_FILE:gen_fixtures> component-multi ${FIXTURES_DIR}
  COMMAND $<TARGET_FILE:gen_fixtures> validator-states ${FIXTURES_DIR}
  COMMAND $<TARGET_FILE:gen_fixtures> pclntab-names ${FIXTURES_DIR}
  COMMAND $<TARGET_FILE:gen_fixtures> brtable-wat ${FIXTURES_DIR}/brtable
  COMMAND $<TARGET_FILE:gen_fixtures> differential ${GEN_DIR} ${FIXTURES_DIR}
  COMMAND $<TARGET_FILE:gen_fixtures> simd-lane-bounds ${FIXTURES_DIR}
  COMMAND $<TARGET_FILE:gen_fixtures> typed-ref-unsupported-gc ${FIXTURES_DIR}
  DEPENDS gen_fixtures
  WORKING_DIRECTORY ${GEN_DIR}
  COMMENT "K2 fixture generators (C++ port of make_*.py)"
  VERBATIM)
