cmake_minimum_required(VERSION 3.16)
# C1 golden runner (phương án B): one C++ executable orchestrating idat with
# a single fixed worker script (tests/golden/oracle_ida.py — the same dumper
# that produced the B1 goldens). All test logic lives in data: fixtures +
# tests/golden/_metrics.json. No SDK dependency, plain C++17.
project(wasmhex_golden_runner LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# idat default: from the IDA bundle discovery shared with tests/runner (env
# IDA_BUNDLE_MACOS_DIR wins, else the newest /IDA*.app at volume root). The
# runtime env WASMDEC_IDAT overrides the baked default either way.
if(NOT DEFINED WASMHEX_IDA_BUNDLE_MACOS_DIR OR "${WASMHEX_IDA_BUNDLE_MACOS_DIR}" STREQUAL "")
  set(WASMHEX_IDA_BUNDLE_MACOS_DIR "" CACHE PATH
      "IDA.app/Contents/MacOS holding idat (default: newest /IDA*.app, or env IDA_BUNDLE_MACOS_DIR)")
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
  set(_wasmdec_idat_default "${WASMHEX_IDA_BUNDLE_MACOS_DIR}/idat")
else()
  set(_wasmdec_idat_default "")
endif()

add_executable(golden_runner golden_runner.cpp)
target_compile_options(golden_runner PRIVATE -Wall -Wextra)
target_compile_definitions(golden_runner PRIVATE
  WASMDEC_IDAT_DEFAULT="${_wasmdec_idat_default}")
unset(_wasmdec_idat_default)
