// Structured diagnostics — every rejection carries a stable error code
// a rejection class, the failing section id and (where known) an offset.
// error_code values are FIXED: never renumber, only append (the fuzz
// invariant and future tooling rely on the numeric values).
// rejection_class_t is the spec/policy/malformed trichotomy; the mapping is
// decided AT THE FAILURE SITE (or via the exact-match table below for the
// legacy `failure_reason` strings) — never by prefix heuristics.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace wasmhex::loader
{

enum class rejection_class_t : std::uint8_t
{
  accepted = 0,
  spec_invalid,       // not valid WebAssembly
  policy_subset,      // valid WebAssembly outside the supported subset
  malformed_encoding, // truncated/oversized low-level encoding
  resource_limit,     // well-formed but beyond an engine resource cap
};

enum class error_code : std::uint16_t
{
  none = 0,

  // -- malformed encodings (reader-level) --
  bad_leb = 1,           // truncated/overflowing LEB128
  bad_section_size = 2,  // section length beyond buffer
  bad_magic = 3,         // missing wasm header
  bad_section_order = 4, // out-of-order or duplicated section

  // -- module structure (spec-invalid) --
  bad_type_section = 10, // type section framing/forms
  bad_import = 11,
  bad_function = 12,
  bad_table = 13,
  bad_memory = 14,
  bad_global = 15,
  bad_export = 16,
  bad_start = 17,
  bad_element = 18,
  bad_data_count = 19,
  bad_code = 20,
  bad_data = 21,
  bad_tag = 22,
  bad_name_section = 23,
  bad_component = 24, // component-model framing

  // -- instruction stream (control scan) --
  bad_instruction = 30,      // unsupported/malformed instruction
  bad_control_flow = 31,     // branches/blocks/labels
  bad_memory_op = 32,        // memarg/addressing
  bad_atomic_op = 33,        // 0xFE subopcodes
  bad_simd_op = 34,          // 0xFD subopcodes
  bad_call = 35,             // direct/indirect/ref calls
  bad_reference_op = 36,     // ref.* / i31
  bad_eh = 37,               // try/catch/throw shapes
  bad_local_assignment = 38, // local.get of an unassigned non-defaultable local
  malformed_immediate = 39,  // instruction immediate failed to decode

  // -- policy subset (valid WebAssembly, unsupported here) --
  policy_gc_types = 40,
  policy_typed_tables = 41,
  policy_table_mutation = 42,
  policy_multiple_memories = 43,
  policy_memory64 = 44,
  policy_shared_memory = 45,
  policy_simd_subset = 46,
  policy_eh_subset = 47,
  policy_call_ref_subset = 48,
  policy_i31_constraints = 49,
  policy_global_type = 50,
  policy_element_segment = 51,
  policy_data_segment = 52,
  policy_import = 53,
  policy_stack_limits = 54,
  policy_other = 55,

  // -- resource limits (well-formed, beyond engine caps) --
  resource_local_count = 60, // function declares too many locals
};

struct diagnostic_t
{
  error_code code = error_code::none;
  rejection_class_t cls = rejection_class_t::accepted;
  std::uint8_t section = 0xFF;
  uint64_t offset = 0;
  std::string detail;

  bool complete() const { return code != error_code::none && cls != rejection_class_t::accepted; }
};

// Exact-match mapping for the legacy `failure_reason` strings emitted by the
// core (module.cpp / core/analysis/control_scan.cpp). Returns error_code::none when the
// string is unknown — callers then fall back to a generic class-based code.
error_code error_code_for_reason(const char *reason);

// Class that belongs to a code (append-only table).
rejection_class_t class_for_code(error_code code);

// Helper for the fuzz invariant: a rejected module must always carry a
// complete diagnostic (code + class). `parsed` is the parse result.
inline bool rejection_has_diagnostic(bool parsed, const diagnostic_t &diag)
{
  return parsed || diag.complete();
}

} // namespace wasmhex::loader
