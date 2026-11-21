// Module-level validation checks - header-only extension for the
// core validator suite. These are the checks the byte-level body pass
// cannot see: global initializer types, element/data offset expressions,
// start signature and local defaultability.
#pragma once

#include "../types.hpp"

#include <cstdint>
#include <vector>

namespace wasmhex::loader
{

struct module_diagnostic_t
{
  bool ok = true;
  const char *reason = nullptr;
  size_t index = 0; // global / segment / function index at fault
};

namespace module_detail
{

// Value-type byte whitelist for globals and locals. Reference handles are
// accepted (approximation: heap subtyping is not modeled).
inline bool known_value_type(uchar type)
{
  switch ( type )
  {
    case 0x7F:
    case 0x7E:
    case 0x7D:
    case 0x7C:
    case 0x7B: // scalars + v128
    case 0x70:
    case 0x6F:
    case 0x6E:
    case 0x6D:
    case 0x6C: // func/extern/...
    case 0x71:
    case 0x72:
    case 0x69: // typed/none/exn
      return true;
    default: return false;
  }
}

// A local whose type is non-defaultable requires an initializer; the MVP
// approximation accepts every reference form that parses (nullable) and
// rejects exnref locals, which never have an initializer in the encodings
// we accept.
inline bool local_type_defaultable(uchar type)
{
  return type != 0x69; // exnref
}

} // namespace module_detail

// Validate the module-level invariants that parse alone does not enforce.
// `memory64` selects the address type (i64) for data/elem offsets.
inline module_diagnostic_t validate_module_level(const module_t &module, bool memory64)
{
  using namespace module_detail;
  module_diagnostic_t out;

  // Globals: known value type, mutability flag, imported ones are exempt
  // from the initializer shape (their value arrives from the host).
  for ( size_t i = 0; i < module.globals.size(); ++i )
  {
    const global_t &global = module.globals[i];
    if ( !known_value_type(global.type) )
    {
      out.reason = "global has an unknown value type";
      out.index = i;
      out.ok = false;
      return out;
    }
    if ( global.mutability > 1 )
    {
      out.reason = "global mutability flag is malformed";
      out.index = i;
      out.ok = false;
      return out;
    }
  }

  // Data segments: constant offsets must be representable in the address
  // type; global.get-placed offsets must reference a global that exists.
  for ( size_t i = 0; i < module.data_segment_sizes.size(); ++i )
  {
    if ( module.data_segment_offsets[i] < 0 && module.data_segment_globals[i] == UINT32_MAX )
      continue; // passive
    if ( module.data_segment_globals[i] != UINT32_MAX
         && module.data_segment_globals[i] >= module.globals.size() )
    {
      out.reason = "data segment offset references a missing global";
      out.index = i;
      out.ok = false;
      return out;
    }
    const int64 offset = module.data_segment_offsets[i];
    if ( offset >= 0 && !memory64 && uint64(offset) > UINT32_MAX )
    {
      out.reason = "data segment offset exceeds the memory32 address type";
      out.index = i;
      out.ok = false;
      return out;
    }
  }

  // Element segments: every populated table slot must address a declared
  // table. elem_entry_t::slot_index is the position INSIDE the table, so
  // the bound check goes through the resolved table slots.
  for ( size_t i = 0; i < module.table_slots.size(); ++i )
  {
    if ( module.table_slots[i].table_index >= module.table_types.size() )
    {
      out.reason = "element segment targets a missing table";
      out.index = module.table_slots[i].table_index;
      out.ok = false;
      return out;
    }
  }

  // Start function: signature must be [] -> [].
  if ( module.start_function_index != UINT32_MAX )
  {
    const uint32 index = module.start_function_index;
    const uint32 total_functions
      = uint32(module.imported_func_types.size() + module.defined_func_types.size());
    if ( index >= total_functions )
    {
      out.reason = "start function index is out of range";
      out.index = index;
      out.ok = false;
      return out;
    }
    const uint32 type_index = index < module.imported_func_types.size()
      ? module.imported_func_types[index]
      : module.defined_func_types[index - module.imported_func_types.size()];
    if ( type_index >= module.types.size() )
    {
      out.reason = "start function references a missing type";
      out.index = index;
      out.ok = false;
      return out;
    }
    const wasm_type_t &type = module.types[type_index];
    if ( !type.params.empty() || !type.results.empty() )
    {
      out.reason = "start function must have signature [] -> []";
      out.index = index;
      out.ok = false;
      return out;
    }
  }

  // Locals: every declared local type must be defaultable (the encodings
  // we accept cannot initialize non-defaultable locals at declaration).
  for ( size_t i = 0; i < module.bodies.size(); ++i )
  {
    const body_t &body = module.bodies[i];
    for ( size_t j = 0; j < body.local_types.size(); ++j )
    {
      if ( !known_value_type(body.local_types[j]) )
      {
        out.reason = "function declares a local with an unknown type";
        out.index = i;
        out.ok = false;
        return out;
      }
      if ( !local_type_defaultable(body.local_types[j]) )
      {
        out.reason = "function declares a non-defaultable local";
        out.index = i;
        out.ok = false;
        return out;
      }
    }
  }
  return out;
}

} // namespace wasmhex::loader
