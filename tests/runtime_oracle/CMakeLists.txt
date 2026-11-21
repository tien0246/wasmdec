# M6-S3: Data-driven C++ WebAssembly runtime oracle (libwasmtime + wabt wasm-interp).
# Note: libwasmtime is resolved from /opt/homebrew (CI story comes in Slice 4).
cmake_minimum_required(VERSION 3.16)
project(wasmhex_runtime_oracle CXX)

find_path(WASMTIME_INCLUDE_DIR NAMES wasmtime.h HINTS /opt/homebrew/include /usr/local/include)
find_library(WASMTIME_LIBRARY NAMES wasmtime HINTS /opt/homebrew/lib /usr/local/lib)

if(WASMTIME_INCLUDE_DIR AND WASMTIME_LIBRARY)
  add_executable(runtime_oracle runtime_oracle.cpp)
  target_compile_features(runtime_oracle PRIVATE cxx_std_17)
  target_include_directories(runtime_oracle PRIVATE "${WASMTIME_INCLUDE_DIR}")
  target_link_libraries(runtime_oracle PRIVATE "${WASMTIME_LIBRARY}")

  add_test(
    NAME runtime_oracle
    COMMAND $<TARGET_FILE:runtime_oracle>
            --cases "${CMAKE_CURRENT_LIST_DIR}/cases.tsv"
            --fixtures "${CMAKE_CURRENT_LIST_DIR}/../fixtures"
    WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../..")
else()
  message(STATUS "libwasmtime not found: runtime_oracle target disabled")
  # Super-build single-add wiring: tests/runner re-adds this directory under
  # the same binary dir guarded on the runtime_oracle TARGET, so when the
  # root super-build (WASMHEX_SUPER_BUILD) configures it without libwasmtime,
  # reserve the target name with a no-op — the runner's guard then sees the
  # directory is already wired and skips. Standalone configures keep the
  # plain "disabled" behavior.
  if(WASMHEX_SUPER_BUILD)
    add_custom_target(runtime_oracle
      COMMENT "runtime_oracle: disabled (libwasmtime not found)")
  endif()
endif()
