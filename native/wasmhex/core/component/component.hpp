// WASM component-model container types.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "../types.hpp"

namespace wasmhex::loader
{

struct embedded_core_module_t
{
  const uchar *data;
  size_t size;
  size_t offset = 0; // file offset of the core-module payload
};

bool find_embedded_core_modules(const uchar *data, size_t size, size_t base_offset, unsigned depth,
                                std::vector<embedded_core_module_t> *modules, std::string *error);

} // namespace wasmhex::loader
