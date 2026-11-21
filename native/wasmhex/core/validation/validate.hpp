// Single validation entry point + rejection classification.
// The core front end rejects inputs for two very different reasons:
// 1. spec_invalid / malformed_encoding — the module is not valid WebAssembly
// (broken structure, bad indices, truncated encodings). Every golden
// oracle `_validator.json` with valid=false MUST land here.
// 2. policy_subset — the module is valid WebAssembly, but wasmhex only
// implements a documented subset and refuses to guess. These rejections
// protect decompilation soundness (e.g. static table-slot resolution is
// only provable when the module cannot mutate the table at runtime).
// The policy table below lists every class-2 rejection family with its
// reason string prefix, the soundness argument and a representative fixture
// from tests/golden whose python validator says valid=true while the core
// rejects it (proving it is policy, not spec).
// POLICY TABLE (spec-valid, deliberately rejected):
//   reason prefix                                          | why (soundness)                                      | representative fixture
//   -------------------------------------------------------+------------------------------------------------------+---------------------------------
//   "Unsupported WebAssembly GC type form"                   | only function types are modeled; typed refs need      | build_wasmhex_typed_ref_unsupported_gc
//                                                           | heap-type metadata the ABI does not carry yet         |
//   "Unsupported typed WebAssembly table"                    | table metadata tracks abstract funcref only          | link_typed_table_nonnull_a
//   "Unsupported WebAssembly table.set mutation"             | static 'G' slot resolution is only sound when no      | backend_segment_indirect_table_mutation
//                                                           | runtime table mutation exists in the module          |
//   "Unsupported WebAssembly table mutation" (init/copy/     | table.init/copy/grow/fill all invalidate slots        | wasmhex_indirect_table_copy_reject
//     grow/fill)                                             | resolved from element segments                       |
//   "Unsupported WebAssembly elem.drop"                      | passive segments are not tracked                     | (crafted; elem.drop = 0xFC:13)
//   "Unsupported WebAssembly element segment; only active"   | passive/declarative element segments have no          | wasmhex_indirect_passive_segment
//                                                           | instantiate-time slot mapping                        |
//   "Unsupported active element segment; table32 funcref"    | active segments must populate funcref tables         | link_expr_funcref_a
//   "Unsupported native wasmhex SIMD memory operation"       | SIMD lowering covers load/store/splat/zero/lane only | native_v128_abi
//   "Unsupported WebAssembly GC instruction"                 | GC instructions beyond ref.i31/i31.get lack ABIs      | (crafted 0xFB rows)
//   "Native wasmhex supports one memory at index zero"       | the memory model maps exactly one linear memory      | (crafted multi-memory)
//   "Native wasmhex supports passive and memory-zero..."     | active data segments must target memory 0            | (crafted)
//   "Native wasmhex EH ..." / legacy try/catch families      | EH lowering implements one direct-call unwind shape  | f5_* fixtures (see golden)
//   "Native wasmhex call_ref requires ..." families          | call_ref lowering needs statically known targets     | call_ref_* fixtures
//   "Native wasmhex i31 ..." families                        | i31 handle lowering requires isolated eq-family use  | wasmhex i31 fixtures
//   "Native wasmhex table.size requires min == max"          | table.size returns a runtime value; only fixed       | (crafted)
//                                                           | tables have a static answer                          |
//   "Native wasmhex indirect dispatch exceeds 65,536..."     | sparse dispatch chains are capped by ABI budget      | (crafted)
//   "Unsupported native wasmhex global import/type"          | global storage models i32/i64/f32/f64 only           | call_ref_direct (global typed ref)
//   "Unsupported active element offset; ..." families        | slot resolution needs constant/known offsets; the    | backend_segment_indirect_* (global.get)
//                                                           | extended-const subset is fixed                        |
//   "Unsupported WebAssembly table limits flags"             | shared/64-bit table limits unmodeled                 | (crafted)
//
// spec_invalid families (must-reject, golden valid=false):
//   section order/duplicates, limits max<min, export kind/name/index,
//   data count mismatch, function body type index, element function index,
//   memory.init/data.drop data index, throw tag index, ref.func index,
//   heap type index bounds, table.size table index bounds.
// malformed_encoding: reader-level failures with no failure_reason set
//   (truncated uleb/sleb, section size out of bounds, bad magic).
#pragma once

#include "../types.hpp"

#include <string>

namespace wasmhex::loader
{

// rejection_class_t moved to core/diagnostic.hpp — re-exported here.
#include "../diagnostic.hpp"

// The honest outcome vocabulary. `valid` stays for source
// compatibility; `status` is the field callers should branch on.
enum class validation_status_t : std::uint8_t
{
  valid = 0,          // module accepted
  invalid = 1,        // spec-invalid or policy rejection (known, classified)
  unsupported = 2,    // constructs the validator does not model (honest
                      // "cannot decide"; tsv bucket: unknown)
  malformed = 3,      // malformed encoding (bad LEB/section size/UTF-8)
  resource_limit = 4, // validator/loader resource ceilings
};

struct validation_verdict_t
{
  bool valid = true;
  validation_status_t status = validation_status_t::valid;
  ::wasmhex::loader::rejection_class_t rejection = rejection_class_t::accepted;
  const char *reason = nullptr; // module_t::failure_reason verbatim
  // module_t::reject copies into owned storage (temporary c_str() callers
  // were use-after-free), so the reason dies with the module. Verdicts outlive their module in the test suites —
  // copy here and point reason at the verdict's own storage.
  std::string reason_storage;
  uchar failure_section_id = 0xFF;
};

// Single entry point for the loader: runs the full front end (parse +
// instruction scans) on the caller-configured module and, on rejection,
// classifies why. Never changes rejection behavior — parse() already
// embedded every check; this wrapper only reports.
bool validate_module(module_t &module, const uchar *data, size_t size,
                     validation_verdict_t *verdict);

} // namespace wasmhex::loader
