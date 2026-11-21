#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../types.hpp"

namespace wasmhex::loader
{

struct mem_dref_t
{
  std::uint32_t body_index = 0;  // module.bodies index
  std::uint32_t insn_offset = 0; // from the body payload start (module.code base)
  std::uint64_t address = 0;     // linear-memory effective address (already + memarg)
  bool store = false;
};

struct global_ref_t
{
  std::uint32_t body_index = 0;
  std::uint32_t insn_offset = 0;
  std::uint32_t global_index = 0;
};

// Abstract-stack constant-address walk over every defined body. Returns the
// number of bodies scanned; *out grows with the proven records (memory
// accesses with constant effective addresses, then global.get sites). A body
// whose stream fails to decode mid-walk simply stops contributing (its
std::size_t const_mem_drefs(const module_t &module, std::vector<mem_dref_t> *out_mem,
                            std::vector<global_ref_t> *out_globals);

} // namespace wasmhex::loader
