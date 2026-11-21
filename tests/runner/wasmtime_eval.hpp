#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace wasmhex_runner
{

// Evaluate export_name(uint32_t) -> int32_t for each (input, expected) pair
// using libwasmtime. Returns true if all cases match.
bool wasmtime_eval_i32_1(const std::string &wasm_path, const char *export_name,
                         const std::vector<std::pair<uint32_t, int32_t>> &cases,
                         std::string *out_error);

} // namespace wasmhex_runner
