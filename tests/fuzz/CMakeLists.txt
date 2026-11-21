# C3 fuzz targets (enable with -DWASMHEX_FUZZ=ON at the root).
#  - fuzz_parse:            libFuzzer target, needs clang with -fsanitize=fuzzer.
#  - fuzz_parse_standalone: any compiler; deterministic mutation driver built
#                           with AddressSanitizer + UndefinedBehaviorSanitizer.
cmake_minimum_required(VERSION 3.16)
project(wasmhex_fuzz CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../native/wasmhex/core/sources.cmake)

set(WASMHEX_SANITIZERS -fsanitize=address,undefined -fno-sanitize-recover=all
                       -fno-omit-frame-pointer)

add_executable(fuzz_parse_standalone fuzz_parse.cpp ${WASMHEX_CORE_REST})
target_link_libraries(fuzz_parse_standalone PRIVATE wasmhex_analysis wasmhex_debuginfo)
target_include_directories(fuzz_parse_standalone PRIVATE ${WASMHEX_CORE_DIR}/..)
target_compile_definitions(fuzz_parse_standalone PRIVATE WASMHEX_FUZZ_STANDALONE)
target_compile_options(fuzz_parse_standalone PRIVATE -g -O1 ${WASMHEX_SANITIZERS})
target_link_options(fuzz_parse_standalone PRIVATE ${WASMHEX_SANITIZERS})

include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LINK_OPTIONS -fsanitize=fuzzer)
check_cxx_source_compiles(
  "int LLVMFuzzerTestOneInput(const unsigned char *data, unsigned long size) { return 0; }"
  WASMHEX_FUZZER_RT_LINKS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND WASMHEX_FUZZER_RT_LINKS)
  add_executable(fuzz_parse fuzz_parse.cpp ${WASMHEX_CORE_REST})
target_link_libraries(fuzz_parse PRIVATE wasmhex_analysis wasmhex_debuginfo)
  target_include_directories(fuzz_parse PRIVATE ${WASMHEX_CORE_DIR}/..)
  target_compile_options(fuzz_parse PRIVATE -g -O1 -fsanitize=fuzzer,address,undefined)
  target_link_options(fuzz_parse PRIVATE -fsanitize=fuzzer,address,undefined)
else()
  # Xcode 26 runners ship a clang whose libclang_rt.fuzzer_osx.a is absent:
  # the link-time probe above fails and the target is skipped instead of
  # breaking the build. fuzz_parse_standalone (run by CI) is unaffected.
  message(STATUS "fuzz_parse skipped: clang fuzzer runtime not linkable")
endif()
