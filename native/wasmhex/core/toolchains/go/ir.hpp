#pragma once

#include <cstdint>
#include <vector>

#include "../../binary/instruction_stream.hpp"

namespace wasmhex::loader
{

struct go_arg_ir_t
{
  std::uint32_t opcode = 0;                // 0x37 i64 / 0x36 i32 / 0x38 f32 / 0x39 f64
  std::uint32_t sp_offset = 0;             // store memarg offset (outgoing-arg area)
  std::uint32_t store_offset = UINT32_MAX; // store opcode offset (in body)
};

struct go_call_ir_t
{
  std::uint32_t store_offset = UINT32_MAX;      // i64.store (return-address push)
  std::uint32_t call_offset = UINT32_MAX;       // call or call_indirect offset
  std::uint32_t fn_index = UINT32_MAX;          // callee index (UINT32_MAX if dynamic)
  std::uint32_t sp_restore_offset = UINT32_MAX; // global.get SP right after call
  std::uint32_t br_offset = UINT32_MAX;         // br_if unwindExit
  std::uint32_t pc_b_const_offset = UINT32_MAX; // i32.const 0 (PC_B push before call)
  std::uint32_t sp_arith_offsets[5] = {
    UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
  }; // local.get L; i32.const K; i32.sub; local.tee L; global.set 0
  std::uint32_t sp_cache_local = UINT32_MAX;
  bool is_pc_f_indirect = false;    // True when call_offset is a PC_F call_indirect
  bool is_pc_f_fast_return = false; // Systemstack fast-path PC_F call_indirect + return
  bool is_noreturn_call = false;    // True when callee is a proven noreturn panic/throw function
  std::uint32_t pc_f_extract_offsets[5] = {
    UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX,
  }; // i64.const 16; i64.shr_u; i32.wrap_i64; local.set 0; local.get 0
  std::vector<go_arg_ir_t> args; // Argument stores in outgoing-argument window

  // True when a call record satisfies the PC_F indirect-call protocol gate
  // (either the standard SP-=8 + store + extract + call_indirect + refresh +
  // br_if protocol, or the runtime.systemstack fast-path extract +
  // call_indirect + refresh + return protocol, with ordered offsets inside
  // `body_size`).
  bool pc_f_protocol_valid(std::uint32_t body_size) const;
};

struct go_sp_refresh_ir_t
{
  std::uint32_t get_offset = UINT32_MAX;  // global.get 0
  std::uint32_t set_offset = UINT32_MAX;  // local.set L
  std::uint32_t local_index = UINT32_MAX; // L (proven equal to sp_cache_local)
};

struct go_jmp_ir_t
{
  std::uint32_t const_offset = UINT32_MAX; // i32.const k
  std::uint32_t set_offset = UINT32_MAX;   // local.set 0 (PC_B)
  std::uint32_t br_offset = UINT32_MAX;    // br
  std::uint32_t case_index = 0;            // k (resume case index)
};

struct go_resume_table_ir_t
{
  std::uint32_t dispatch_offset = UINT32_MAX; // first br_table with selector local.get 0
  std::vector<std::uint32_t> targets;         // in-body target offsets per arm (+ default)
  std::vector<std::uint32_t> depths;          // branch depth per arm (+ default)
};

// One proven result-slot store. Go wasm callees write their real results
// to the incoming-argument window at [SP + frame_size + 8 + off] with
// off >= param_window, adjacent (same basic block) to the frame epilogue
// `local.get 1; i32.const frame_size; i32.add; local.tee 1; global.set 0`
// (the frame-size restore) that precedes `i32.const 0; return`.
struct go_result_ir_t
{
  std::uint32_t opcode = 0;                // store opcode (0x36..0x3E)
  std::uint32_t sp_offset = UINT32_MAX;    // offset past the [SP+frame_size+8] base
  std::uint32_t store_offset = UINT32_MAX; // store opcode offset (in body)
};

struct go_prologue_ir_t
{
  std::uint32_t sp_get_offset = UINT32_MAX;      // local.get L (SP-cache left operand)
  std::uint32_t guard_load_offset = UINT32_MAX;  // i32.load *(g + 16)
  std::uint32_t guard_const_offset = UINT32_MAX; // i32.const K (UINT32_MAX = small frame)
  std::uint32_t framesize_extra = 0;             // K (0 = small frame, direct compare)
  std::uint32_t compare_offset = UINT32_MAX;     // i32.le_u / i32.lt_u
  std::uint32_t if_offset = UINT32_MAX;          // `if` (blocktype void); UINT32_MAX = unproven
  std::uint32_t store_offset = UINT32_MAX;       // resume-PC i64.store a=3 off=0
  std::uint32_t call_offset = UINT32_MAX;        // call runtime.morestack[_noctxt]
  std::uint32_t br_if_offset = UINT32_MAX;       // br_if (redo)
  std::uint32_t end_offset = UINT32_MAX;         // `end` closing the if

  bool prologue_valid(std::uint32_t body_size) const;
};

// One proven Go write-barrier slow-path site. The Go
// compiler (cmd/internal/obj/wasm) emits, around every pointer write guarded
// by runtime.writeBarrier.enabled:
//   local.get L; i64.eqz; i32.eqz; if {    <-- guard (fast path branches
//     i32.const K; local.set 0; br D          away when the barrier is off)
//   } end
//   [local.get X; eqz; if { SP-=8; [SP]=PC; i32.const 0; call
//     runtime.sigpanic; SP refresh; br_if R } end]  <-- optional in-body
//     nil guard (duplicated by the compiler in the shared post-store path)
//   <arity+1 spill stores [SP+off] = args / old value>
//   call runtime.gcWriteBarrier{1..8}
//   global.get 0; local.set 1              <-- SP refresh
//   local.tee B; i32.wrap_i64              <-- wbBuf slot pointer (result)
//   <arity buffer writes: [B + k*8] = [SP + src_k], k = 0..arity-1>
//   <reload units: local.get 1; [SP+off]; local.set X>
// The record proves the WHOLE protocol from the real compiler bytecode
// (every opcode verified at its recorded offset), replacing the ctree shape
// verifier that used to live in decompiler/rewriter_go.cpp (try_collapse_wb_if
// + its ~20 reconciliation helpers). Consumers only need `guard_if_offset`
// (mapped to an EA) and `arity`; the other fields are evidence for tests.
struct go_wb_site_ir_t
{
  std::uint32_t guard_if_offset = UINT32_MAX; // the guard `if` (slow path falls through)
  std::uint32_t call_offset = UINT32_MAX;     // call runtime.gcWriteBarrier{1..8}
  std::uint32_t arity = 0;                    // 1..8 from the callee identity
  std::uint32_t buf_local = UINT32_MAX;       // local.tee B (wbBuf slot pointer)
  std::uint32_t spill_count = 0;              // proven spill stores before the call
  bool has_in_body_nil_guard = false;         // sigpanic guard inside the slow path
  bool has_old_value_chain = false;           // old-value load+spill proven

  bool wb_site_valid(std::uint32_t body_size) const;
};

struct go_function_ir_t
{
  bool present = false; // true when built under a verified modern Go profile

  // G10: the proven morestack stack-check prologue (if_offset ==
  // UINT32_MAX when unproven; one per body — the Go compiler emits a
  // single stack check).
  go_prologue_ir_t prologue;

  // Go write-barrier slow-path sites in this body (empty when
  // none proven; sorted by guard_if_offset). The ctree layer consumes the
  // marks — it never re-derives the protocol.
  std::vector<go_wb_site_ir_t> wb_sites;

  std::uint32_t unwind_exit = UINT32_MAX;      // outer block $unwindExit offset
  std::uint32_t entry_point_loop = UINT32_MAX; // outer loop $entryPointLoop offset
  go_resume_table_ir_t resume_table;
  std::vector<go_jmp_ir_t> jmps;
  std::vector<go_call_ir_t> calls;
  std::vector<go_sp_refresh_ir_t> sp_refresh;
  std::vector<std::uint32_t> morestack; // call offsets to runtime.morestack[_noctxt]
  std::vector<std::uint32_t> ret;       // return instruction offsets
  bool pc_b_register_proof = false;     // local 0 used only as PC_B / indirect-call temp
  bool is_pc_f_loop = false;            // proven 41-byte wasm_pc_f_loop trampoline
  std::uint32_t pc_f_loop_call_offset = UINT32_MAX; // call_indirect offset in wasm_pc_f_loop

  // Go frame evidence (blob 'x' tail GOFR v1).
  // `frame_size` comes from the case-0 prologue `local.get 1; i32.const K;
  // i32.sub; local.tee 1; global.set 0` (UINT32_MAX = unproven). The
  // incoming-argument window [SP + frame_size + 8, +256) is split into a
  // param lower bound (`param_window`, max scalar-LOAD end — params are never
  // stored in a proven body) and `results` (stores at/after the param window
  // that feed a frame epilogue). `param_window` is a LOWER bound: params the
  // body never loads are invisible to it, so consumers must cross-check the
  // result slots against caller-side post-call reads before typing them.
  std::uint32_t frame_size = UINT32_MAX;
  std::uint32_t param_window = 0;
  std::vector<go_result_ir_t> results;

  static constexpr std::uint32_t kResultsNone = 0;
  static constexpr std::uint32_t kResultsProven = 1;
  static constexpr std::uint32_t kResultsNoCaller = 2;
  static constexpr std::uint32_t kResultsMismatch = 3;
  static constexpr std::uint32_t kResultsAmbiguous = 4;
  std::uint32_t results_caller_state = kResultsNone;

  bool results_caller_proven() const { return results_caller_state == kResultsProven; }

  // True when the frame evidence preconditions hold (verified IR with a
  // resume dispatcher, proven frame_size, bounded param window, and every
  // result record in-body with sp_offset inside [param_window, +64)).
  bool frame_valid(std::uint32_t body_size) const;

  // True when the `wasm_pc_f_loop` trampoline preconditions hold
  // (verified IR, `is_pc_f_loop`, and `pc_f_loop_call_offset` inside `body_size`).
  bool pc_f_loop_valid(std::uint32_t body_size) const;

  // True when the dispatcher-replacement + const-JMP unflattening
  // preconditions hold (verified IR, valid dispatcher with >=2 arms,
  // pc_b_register_proof, and every const-JMP case_index in
  // [0, targets.size()-1) with an in-body target; no size gate).
  bool unflattening_valid(std::uint32_t body_size) const;
};

struct module_t;

bool is_go_terminal_noreturn_function(const go_function_ir_t &ir, bool is_morestack_or_noctxt);

// Build go_function_ir_t for every body in `module` when `module.go_profile`
// is verified (modern Go wasm: go1_23_series, go1_26_series, go1_27_series)
// and `module.go_provenance` holds. Legacy 1.19 or unknown/none profiles
// skip IR creation. Also populates `body_t::go_calls` with the
// outgoing-argument boundary filter applied.
void build_go_function_irs(module_t &module);

// Exact pclntab/export-name recognizer for `runtime.gcWriteBarrier{1..8}`
// (moved verbatim from decompiler/rewriter_go.cpp — the loader decides callee
// identity, the ctree layer never matches names again).
bool match_gc_write_barrier_name(const char *name, std::uint32_t *out_arity);

// Exact name recognizer for `runtime.sigpanic` (nil-check guard inside
// Go write-barrier slow paths). Stripped `wasm_func_N` names never match.
bool match_sigpanic_name(const char *name);

// Proven bytecode recognizer for the `gcWriteBarrier{1..8}` WRAPPER
// bodies (`i64.const N*8; call <core>; return; unreachable; end`) — the
// stripped-module callee identity source.
bool match_gc_write_barrier_wrapper_body(const std::uint8_t *data, std::size_t size,
                                         std::uint32_t *out_arity,
                                         std::uint32_t *out_core_func_idx);

// Proven bytecode recognizer for the underlying `gcWriteBarrier` core
// (the wbBuf bump-allocation loop prologue).
bool match_gc_write_barrier_core_body(const std::uint8_t *data, std::size_t size);

// Serialize / deserialize `go_function_ir_t` into the netnode 'x' uint32 word
// stream (version 3: v2 header+calls prefix followed by the IR tail so v2
// readers and corruption tests stay compatible).
void encode_go_function_ir_blob(const go_function_ir_t &ir, std::vector<std::uint32_t> *out);
bool decode_go_function_ir_blob(const std::uint32_t *words, std::size_t word_count,
                                go_function_ir_t *out);

// One deduplicated result slot (sp_offset + store width + store
// opcode), sorted by (sp_offset, width).
struct go_result_slot_t
{
  std::uint32_t sp_offset = 0;
  std::uint32_t width = 0;
  unsigned char opcode = 0;
};

// The deduplicated, sp_offset-sorted result-slot list. A Go body
// stores the same slot once per `return` site, so identical (offset, width)
// pairs collapse. Returns false when the deduplicated byte ranges overlap
// (ambiguous slot layout).
bool go_result_slots_sorted(const go_function_ir_t &ir, std::vector<go_result_slot_t> *slots);

// Map the frame-evidence result-slot stores to wasm result types
// sp_offset-sorted and deduplicated. One word per slot by store opcode:
// i64/f64/f32 stores keep their type; every narrower integer store
// (including store8/store16 bools) displays as i32. Returns false when the
// deduplicated byte ranges overlap (ambiguous slot layout).
// Purity note: this does NOT check `results_caller_proven` — callers gate.
bool go_result_slot_types(const go_function_ir_t &ir, std::vector<unsigned char> *types);

enum class go_sret_slot_verdict_t
{
  kNeutral = 0,
  kAligned = 1,
  kBoundArgument = 2,
};

go_sret_slot_verdict_t go_sret_result_slot_verdict(const module_t &module, std::size_t body_index);

// One ArgInfo-shaped caller parameter (the loader-side mirror of the
// walk in decompiler/filter.cpp collect_go_call_shapes).
struct go_shape_param_t
{
  bool is_group = false;
  std::uint32_t offset = 0;
  std::uint32_t bytes = 0;
};

struct go_arginfo_t; // core/toolchains/go/types.hpp (types.hpp includes this
// header first, so only a forward declaration is possible here)

// Expand the per-callee ArgInfo record into the caller-side shape
// parameters (a scalar is one word unless the slot is 8 bytes; an aggregate
// group is one pointer word). False on an impossible encoding.
bool go_arginfo_shape_params(const go_arginfo_t &arginfo, std::vector<go_shape_param_t> *params);

// Decode the full instruction stream of one body (the
// shared cursor walk — same records the IR builder and the decompiler's
// collectors materialize their views from). Empty when the body cannot
// decode.
bool decode_go_body_insns(const module_t &module, std::size_t body_index,
                          std::vector<binary::decoded_insn_t> *insns);

// Prove 16-byte js.Value package-level globals by
// receiver usage. For every recorded Go call whose callee is a known-API
// syscall/js method with a js.Value receiver, the two receiver window
// stores (arg sp_offset 0 and 8) must both be constant-address loads:
//   [i64.const A; i32.wrap_i64; i64.load a=3 off=0; local.set L]
// referenced by the store's `local.get L` value operand (A at offset 0,
// A+8 at offset 8). `out` receives the deduplicated, sorted base
// addresses A (linear-memory offsets). Fail-closed: any other value
// shape (computed address, non-adjacent pair, mixed widths) proves
// nothing for that call.
bool go_jsvalue_receiver_globals(const module_t &module, std::vector<std::uint64_t> *out);

} // namespace wasmhex::loader
