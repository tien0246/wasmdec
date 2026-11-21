#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../types.hpp"

namespace wasmhex::loader
{

// Known libc APIs — pointer PARAMETER POSITIONS only (bit j = parameter j).
// Deliberately tiny: entries exist only where the C standard fixes the
// position as a pointer (or the format position of a printf/scanf family).
// A name not in the table proves nothing.
struct libc_pointer_api_t
{
  const char *name;
  std::uint64_t pointer_mask;
};

const libc_pointer_api_t *find_libc_pointer_api(const std::string &name);

// Convenience: the pointer-position mask for `name`, 0 when unknown.
std::uint64_t libc_pointer_mask(const std::string &name);

// A constant argument at a proven pointer position (recorded on the final
// fixpoint pass; the loader turns each into a dref so an item exists at the
// target, and a per-call-EA blob the decompiler swap consumes — the
// constant may reach the call through a local, which the operand no longer
// carries by then).
struct pointer_arg_record_t
{
  std::uint32_t body_index = 0;  // module.bodies index
  std::uint32_t insn_offset = 0; // from the body payload start
  std::uint32_t param_index = 0; // argument position (0-based)
  std::uint64_t address = 0;     // the constant linear-memory address
};

struct param_pointer_result_t
{
  // Per defined body, in body order (function index =
  // imported_func_types.size() + i): bit p = parameter p proven used as a
  // pointer in that body.
  std::vector<std::uint64_t> proven_param_masks;
  std::vector<pointer_arg_record_t> arg_records;
};

// Run the fixpoint walk over every defined body. `table_masks` carries the
// known-libc pointer mask per function index (imports AND named defined
// functions — the caller builds it from module.function_imports /
// module.function_names); `memory_limit` gates the recorded constant
// arguments (0 = no linear-memory segment: nothing is recorded). Passes are
// capped; an under-converged walk simply proves fewer positions (the safe
// direction).
param_pointer_result_t param_pointer_walk(const module_t &module,
                                          const std::vector<std::uint64_t> &table_masks,
                                          std::uint64_t memory_limit);

} // namespace wasmhex::loader
