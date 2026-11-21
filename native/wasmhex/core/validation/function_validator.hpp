// Typed operand-stack validation for one function body.
//
// Implements the validation algorithm of the WebAssembly specification
// (Appendix "Validation Algorithm"): a typed value stack, a control-frame
// stack, label types, and stack polymorphism after unconditional branches.
// It is deliberately independent of module_t::scan_control(): the scanner
// collects facts for lowering, this pass decides whether the body is valid.
//
// Coverage: MVP, sign extension, non-trapping float-to-int, bulk memory,
// reference types, multi-value, tail calls, multi-memory memargs and
// memory64/table64 address types. Reference subtyping is approximated: only
// funcref vs externref mismatches are rejected, so GC subtyping can never be
// rejected by mistake. Opcode families that are not modeled yet (SIMD,
// threads, GC, exception handling, typed function references) stop the
// check for that function and report it as incomplete instead of guessing.
#pragma once

#include "../diagnostic.hpp"
#include "../types.hpp"

namespace wasmhex::loader
{

// Five-way state. `unsupported` is NOT a clearance — a function that
// stopped at an unmodeled opcode family has not been validated, and callers
// must not report it as checked. `malformed` keeps encoding failures apart
// from spec violations; `resource_limit` marks engine-cap rejections.
enum class function_validation_state_t : uchar
{
  valid,          // fully type-checked, no violation found
  invalid,        // definitely invalid WebAssembly
  unsupported,    // stopped at an unmodeled opcode family — NOT validated
  malformed,      // an immediate failed to decode
  resource_limit, // rejected by an engine resource cap
};

struct function_validation_t
{
  function_validation_state_t state = function_validation_state_t::valid;
  // Legacy two-bit view of the state (kept for existing callers):
  //   valid    = state is valid|unsupported   (not *definitely* invalid)
  //   complete = state is valid               (nothing was left unchecked)
  bool valid = true;
  bool complete = true;
  size_t offset = 0; // byte offset of the failure / stop in the body
  uchar opcode = 0;  // opcode at the failure / stop
  const char *reason = nullptr;
  error_code code = error_code::none; // structured code
};

// `body` points at the first instruction (after the local declarations);
// `local_types` holds the declared locals only (parameters come from
// `function_type`). Types use the canonical bytes produced by
// module_t::read_value_type().
function_validation_t validate_function_body(const module_t &module,
                                             const wasm_type_t &function_type,
                                             const std::vector<uchar> &local_types,
                                             const uchar *body, size_t size,
                                             const std::vector<uchar> *local_ref_forms = nullptr);

} // namespace wasmhex::loader
