# M6-S3: C4 differential runtime tools (all-C++ pipeline).
# Note: libwasmtime is resolved from /opt/homebrew (CI story comes in Slice 4).
cmake_minimum_required(VERSION 3.16)
project(wasmhex_diffrt C CXX)

add_executable(diffrt_gen_inputs gen_inputs.cc)
target_compile_features(diffrt_gen_inputs PRIVATE cxx_std_17)

add_executable(diffrt_transform transform.cc)
target_compile_features(diffrt_transform PRIVATE cxx_std_17)

add_executable(diffrt_compare compare.cc)
target_compile_features(diffrt_compare PRIVATE cxx_std_17)

find_path(WASMTIME_INCLUDE_DIR NAMES wasmtime.h HINTS /opt/homebrew/include /usr/local/include)
find_library(WASMTIME_LIBRARY NAMES wasmtime HINTS /opt/homebrew/lib /usr/local/lib)

if(WASMTIME_INCLUDE_DIR AND WASMTIME_LIBRARY)
  add_executable(diffrt_wasm_runner wasm_runner.c)
  target_include_directories(diffrt_wasm_runner PRIVATE "${WASMTIME_INCLUDE_DIR}")
  target_link_libraries(diffrt_wasm_runner PRIVATE "${WASMTIME_LIBRARY}")
else()
  message(STATUS "libwasmtime not found: diffrt_wasm_runner target disabled")
endif()
