#pragma once
#include "../../../ida/processor/internal.hpp"
#include "../../../core/toolchains/go/index.hpp"
#include <map>
#include <set>
#include <string>
namespace wasmhex
{
class wasmhex_filter_t final : public microcode_filter_t
{
  mba_t *state_mba = nullptr;
  ea_t state_entry = BADADDR;
  bool state_ready = false;
  func_type_data_t function_type;
  qvector<uchar> function_params;
  qvector<uchar> function_results;
  qvector<uchar> declared_local_types;
  qvector<wasm_local_t> locals;
  qvector<wasm_value_t> stack;
  qvector<wasm_control_frame_t> controls;
  qvector<wasm_branch_resume_t> resume_states;
  qvector<wasm_indirect_dispatch_t> indirect_dispatches;
  std::unordered_map<uint64_t, mreg_t> operand_register_index;
  std::unordered_map<uint64_t, mreg_t> scratch_register_index;
  mop_t branch_table_selector;

  mreg_t sret_pointer_reg = mr_none;
  mreg_t boolean_scratch_reg = mr_none;
  wasmhex_bounds_mode_t bounds_mode = wasmhex_bounds_mode_t::explicit_bounds;
  wasmhex_c_frame_metadata_t c_frame_metadata;
  bool has_c_frame = false;
  // C shadow stack tracked by dataflow (no loader prologue
  // pattern). The frame extent/direction are learned at runtime from the
  // first constant sp adjustment.
  bool soft_c_frame = false;
  bool soft_frame_learned = false;
  // Set once a gojs./go. import call is seen (module-level 'G').
  bool go_module_recognized = false;
  // C-frame fix: EA of the instruction currently being lowered (set in
  // apply) — fail messages from the abstract-stack helpers use it.
  ea_t current_insn_ea = BADADDR;
  // Call-protocol plumbing sites (blob 'x'). apply() pops
  // the operands at these EAs but emits nothing, so the goroutine
  // plumbing stays out of the presentation.
  qvector<ea_t> go_call_store_eas;
  qvector<ea_t> go_call_sp_refresh_eas;
  qvector<ea_t> go_call_br_eas;
  // Subset of `go_call_br_eas` belonging to calls whose callee is a
  // proven noreturn panic/throw function (`call.is_noreturn_call`). After
  // consuming the post-call `br_if $unwindExit`, `apply()` emits
  // `emit_unreachable(cdg)` so the cold panic block terminates instead of
  // falling through into the next panic block.
  qvector<ea_t> go_call_noreturn_br_eas;
  // The `local.set L` of each SP-refresh pair, keyed by EA and
  // carrying the SP-cache local index. The lowering used to consume "the
  // next local.set after the refresh" (order flag, no EA/index proof) —
  // any local.set the codegen visits in between was swallowed silently.
  // Consumption is now exact: the pair `global.get 0; local.set L` is
  // verified in the decoded stream during collection and apply() consumes
  // that EA, checking the decoded local index still matches.
  std::map<ea_t, uint32> go_call_sp_set_locals;
  // Per-site argument info — arg stores to consume (value kept on
  // the stack for the call), the PC_B zero constant right before the
  // call, and the wasm argument types keyed by call EA so emit_call can
  // render the Go signature instead of the raw (i32) one.
  std::map<ea_t, uchar> go_call_arg_store_types;
  qvector<ea_t> go_call_const_eas;
  std::map<ea_t, qvector<uchar>> go_call_args_by_call;
  // ArgInfo-shaped aggregate grouping at recorded Go call sites
  // (blob 't', module-level type layer). One shape per call EA: the merged
  // parameter list — scalar parameters keep their word arguments, every
  // PROVEN top-level aggregate group (ArgInfo StartAgg/EndAgg boundaries,
  // byte-exact against the caller's outgoing-window stores) becomes ONE
  // pointer argument whose value is the group's own window address (the
  // address operand of the group's first store, kept on the abstract
  // stack). Calls without a shape entry render exactly as before
  // (fail-closed: no blob, no ArgInfo, a layout disagreement or an
  // unrecognizable store shape all leave the call alone).
  struct go_call_param_t
  {
    bool is_group = false; // false: scalar word; true: aggregate group
    uint32 offset = 0;     // outgoing-window byte offset
    uint32 bytes = 0;      // scalar: slot bytes; group: aggregate extent
    uchar type = 0;        // scalar: wasm value type; group: 0x7F (pointer)
    qstring group_type;    // group: C pointee type name ("go_slice" ...)
    // A descriptor-proven NAMED group ("go__js__Value"
    // "go__main_Pt"): the field list registers the til struct at emit time
    // (memoized, the same discipline as ensure_go_aggregate_type). Empty
    // for the canonical layouts above.
    std::vector<loader::go_c_field_t> group_fields;
    qvector<ea_t> window_store_eas;
    bool fold_single_use = false;
    bool fold_global_source = false;
    uint64 fold_global_address = 0;
    bool fold_const_pair = false;
  };
  struct go_call_shape_t
  {
    qvector<go_call_param_t> params;
    uint32 sp_local = UINT32_MAX; // the call's SP-cache local (window base)
    // The callee is variadic (blob-'t' layout) and the trailing
    // []any group is proven all-zero (every slot an `i64.const 0` store).
    // The emit side drops the parameter and consumes the stores — the call
    // renders f(a, b) exactly like the Go source with no variadic args.
    bool empty_variadic_args = false;
    qvector<ea_t> empty_args_store_eas;
    // The consumed single-use window stores (fold_* on the
    // parameters) — apply() pops them with the call plumbing.
    qvector<ea_t> fold_store_eas;
  };
  std::map<ea_t, go_call_shape_t> go_call_shapes;
  // The `i64.const 0` stores of proven EMPTY variadic []any
  // arguments (go_call_shape_t::empty_args_store_eas) — consumed with the
  // call plumbing so the dead zero fills never reach the presentation.
  std::set<ea_t> go_empty_args_store_eas;
  // Every proven single-use window store (groups folded into call
  // expressions) — consumed exactly like the empty-args fills.
  std::set<ea_t> go_window_fold_store_eas;
  std::set<ea_t> go_call_group_stores;
  // EAs of the SP-decrement leg (local.get L; i32.const K; i32.sub;
  // local.tee L; global.set 0) in front of each recorded store —
  // recovered from the decoded stream (blob 'x' has no offsets for it)
  // and consumed with the other legs so the cached SP stops drifting
  // -8 per call.
  std::set<ea_t> go_call_sp_arith_eas;
  // `go_pcf_extract_eas` consumes `i64.const 16; i64.shr_u; i32.wrap_i64;
  // local.set 0; local.get 0` as stack-neutral so the 8-byte funcval target
  // stays on top of the abstract stack; `go_pcf_call_fn_by_ea` maps the
  // `call_indirect` EA to `fn_index` (`< UINT32_MAX` when a constant PC_F
  // resolved to a direct callee, `UINT32_MAX` when dynamic `m_icall`).
  std::set<ea_t> go_pcf_extract_eas;
  std::map<ea_t, uint32> go_pcf_call_fn_by_ea;
  // `wasm_pc_f_loop` outer trampoline `call_indirect` site proven
  // by the Go function IR (`loaded_go_ir.pc_f_loop_valid`), lowered as an O(1)
  // checked indirect table call instead of the full-module sparse cascade.
  ea_t go_pcf_loop_call_ea = BADADDR;
  // Caller-cross-checked Go result slots of THIS function (blob 'x'
  // GOFR v2 `kResultsProven`, deduplicated + sp_offset-sorted, one entry per
  // displayed result). When `go_results_synthesis` is set, emit_return
  // discards the dead RETUNWIND i32 push and re-loads each result from
  // [SP_local + frame_size + 8 + slot] instead (the Go wasm ABI: results
  // live in the incoming window past the params; at the return the cached SP
  // local already points at the window base).
  bool go_results_synthesis = false;
  std::vector<loader::go_result_slot_t> go_result_slots;
  struct go_jmp_t
  {
    ea_t const_ea = BADADDR;
    ea_t set_ea = BADADDR;
    ea_t br_ea = BADADDR;
    uint32 case_index = 0;
  };
  std::vector<go_jmp_t> go_jmps;
  std::map<ea_t, size_t> go_jmp_by_const;
  std::map<ea_t, size_t> go_jmp_by_set;
  std::map<ea_t, size_t> go_jmp_by_br;
  ea_t go_dispatch_ea = BADADDR;
  ea_t go_dispatch_selector_ea = BADADDR;
  qvector<ea_t> go_dispatch_targets;
  qvector<uint32> go_dispatch_depths;
  size_t go_direct_edges = 0; // Trace: direct edges actually emitted
  uint32 soft_sp_index = UINT32_MAX;
  // Prototype: the current function rides its proven Go frame
  // (blob 'x' GOFR: frame_size + param window + result slots) through the
  // C shadow-stack machinery — SP-relative cells [0, frame_size) map to
  // decompiler stack vars, the incoming window [frame_size+8, +extent) maps
  // to the args area (inargoff + off). Unlike the C path, global.get/set 0
  // keep their plain-global semantics (Go's SP reads outside the consumed
  // prologue/epilogue/call legs are the morestack guard); the frame anchor
  // comes from the SP-cache local the prologue leg writes.
  bool go_frame_mode = false;
  std::set<ea_t> go_frame_leg_eas;
  std::set<ea_t> go_receiver_wrap_eas;
  // The per-function decode-once cache (instruction stream +
  // decoded blob 'x' + 'j' dispatch memos). Set at reset_state, cleared in
  // clear_runtime_state — never held across functions (LRU eviction would
  // dangle it; the bar's asan-smoke exists to catch exactly that).
  loader::function_ir_t *active_function_ir = nullptr;
  bool c_frame_fp_written = false;
  bool c_frame_sp_read = false;
  bool has_sret_pointer = false;
  bool unreachable = false;
  bool function_end_emitted = false;
  bool logged_unknown_helper = false;

  void clear_runtime_state();

  bool reset_state(codegen_t &cdg);

  bool collect_go_call_sp_arith(const qvector<ea_t> &store_eas, std::set<ea_t> *sp_arith_eas,
                                std::map<ea_t, uint32> *sp_cache_local_by_store = nullptr);

  bool collect_go_call_sp_refresh_sets(const qvector<ea_t> &refresh_eas,
                                       std::map<ea_t, std::pair<ea_t, uint32>> *refresh_info);

  // Walk the decoded stream once and collect the Go frame legs
  // the case-0 prologue `local.get L; i32.const K; i32.sub; local.tee L;
  // global.set 0` (K == frame_size, L = the SP-cache local that becomes
  // the frame anchor) and every epilogue `local.get L; i32.const K+8;
  // i32.add; local.tee L; global.set 0` (frame + 8 = the pushed return
  // address slot). All leg instructions are consumed stack-neutrally so
  // the decompiler frame is fixed. Returns false (go-frame mode stays off)
  // when no prologue shape matches.
  bool collect_go_frame_legs(const loader::go_function_ir_t &go_ir);

  // Populate the entryPointLoop dispatcher plus every const-JMP record
  // from the pre-built Go function IR (blob 'x' v3). Returns false
  // (fail-closed, everything cleared) when the IR is absent or the shape
  // does not match — the dispatcher loop then lowers as before.
  bool collect_go_jmps(const loader::go_function_ir_t &go_ir);

  void collect_go_call_shapes(netnode &wasm_node, const loader::go_module_index_t &index,
                              const loader::go_function_ir_t &go_ir);

  // Descriptor-anchored type notes for this function's recorded Go
  // calls — `// go:` annotations set as idempotent regular instruction
  // comments at the call/store EA (makemap* call sites whose first window
  // argument is a constant map descriptor, and closure code words
  // `slot << 16` resolving through blob 'Z' to an anchored function).
  // The type layer and the table-0 slot map come from the ModuleIndex.
  // Fail-closed per site; never fails the decompile.
  void collect_go_type_notes(netnode &wasm_node, const loader::go_module_index_t &index,
                             const loader::go_function_ir_t &go_ir);

  // Value display over proven evidence — constant (ptr, len)
  // string pairs at shaped call arguments (rendered as C literals at ctree
  // maturity), inline interface boxing pairs and proven variadic backing
  // arrays (`// go: box(...)`, `// go: args = {...}`), and receiver member
  // accesses on the proven Go frame (`// go: a2->field`). Runs after the
  // go-frame decision (needs the frame anchor local + frame size); the
  // descriptor layer comes from the ModuleIndex. All fail-closed per site;
  // never fails the decompile.
  void collect_go_value_evidence(netnode &wasm_node, const loader::go_module_index_t &index,
                                 const loader::go_function_ir_t &go_ir);

  // Lower the Go entryPointLoop resume dispatcher by replacing the
  // switch(PC_B) with a direct edge to the main entry block (PC_B == 0)
  // while queuing resume states for all block arms so secondary resume
  // blocks decode with a balanced abstract stack.
  merror_t emit_go_dispatcher_replacement(codegen_t &cdg);

  // Lower a recorded const-JMP as a direct edge to the resume block
  // of dispatch case k (lower_control.cpp).
  merror_t emit_go_direct_jmp(codegen_t &cdg, const go_jmp_t &record);

  wasm_indirect_dispatch_t *find_indirect_dispatch(ea_t source)
  {
    for ( wasm_indirect_dispatch_t &dispatch : indirect_dispatches )
      if ( dispatch.source == source )
        return &dispatch;
    return nullptr;
  }

  // The stub-stream bookkeeping (get_indirect_dispatch
  // mark_indirect_dispatch_stub, all_indirect_dispatches_processed) is
  // retired together with the call-dispatch stubs — dispatches are found
  // by source EA alone (find_indirect_dispatch).

  void prune_closed_controls(ea_t ea);

  bool emit_move(codegen_t &cdg, const mop_t &src, const mop_t &dst, bool fp);

  bool emit_call_result_move(codegen_t &cdg, const mop_t &src, const mop_t &dst, bool fp);

  bool make_local_storage(codegen_t &cdg, uchar type, mop_t *local);

  bool make_operand_register(codegen_t &cdg, uchar type, size_t depth, mop_t *value);

  bool make_scratch_register(codegen_t &cdg, int size, size_t index, mop_t *value,
                             mreg_t *out_reg = nullptr);

  bool make_boolean_register(codegen_t &cdg, mop_t *value);

  static bool is_c_frame_marker(const wasm_value_t &value);

  static bool is_table_base_marker(const wasm_value_t &value);

  merror_t reject_c_frame_marker_use(codegen_t &cdg, const char *context);

  // Materialize a C-frame address marker (fp+k) as an explicit
  // mop_a stack-slot address so it can flow through ordinary scalar paths
  // (local.set/tee of address-taking locals, fp+dynamic offset arithmetic).
  // Returns false when the marker cannot be represented (sp base outside
  // the frame, out-of-range offset).
  bool materialize_c_frame_marker(codegen_t &cdg, wasm_value_t *value);

  // C-frame fix: in-frame marker -> stack slot; everything else -> the
  // real sp_global+offset linear address (call args past the frame end,
  // pointer tagging, negative offsets).
  bool resolve_c_frame_scalar(codegen_t &cdg, wasm_value_t *value, size_t depth);
  // A materialized frame-cell address is 4 bytes on
  // wasm32, but every 8-byte consumer of one (an i64 local slot, an i64
  // arithmetic operand, an 8-byte store value, an 8-byte declared call
  // argument) must receive it zero-extended exactly once, otherwise the
  // consumer is an m_mov/m_add/m_stx size mismatch (INTERR
  // 50836/50831/50817). The same rule the call-argument and store-value
  // sites already applied inline, centralized for the remaining sinks.
  bool widen_materialized_address(codegen_t &cdg, mop_t *op, int width, size_t index);
  // Coarse fallback - a C-frame marker as a plain linear-memory
  // address computed from the shadow stack pointer global.
  bool lower_c_frame_marker_address(codegen_t &cdg, const wasm_value_t &address,
                                    size_t address_depth, mop_t *out);
  // Map a constant linear-memory offset onto the loaded DATA
  // segment (loader tag 'L') as a global reference.
  bool resolve_data_address(uint64 linear_offset, int size, mop_t *out);

  void push_c_frame_address(int32 offset);

  void push_c_sp_base();

  merror_t spill_value(codegen_t &cdg, wasm_value_t *value, size_t depth);

  bool read_control_types(ea_t ea, uchar tag, qvector<uchar> *types, size_t max_types = 1);

  bool alloc_local(codegen_t &cdg, size_t index, bool read_before_write);

  void push(const mop_t &op, uchar type, uint32 function_ref_index = UINT32_MAX,
            bool owned_temp = false);

  bool pop(uchar type, wasm_value_t *out, bool allow_c_frame_marker = false);

  merror_t emit_drop(codegen_t &cdg);

  merror_t emit_select(codegen_t &cdg, uchar expected_type = 0);

  merror_t emit_constant(codegen_t &cdg, uchar type);

  bool fold_c_frame_arithmetic(codegen_t &cdg, const wasm_value_t &left, const wasm_value_t &right,
                               bool subtract);

  bool fold_table_base_arithmetic(codegen_t &cdg, const wasm_value_t &left,
                                  const wasm_value_t &right, bool subtract);

  merror_t emit_add(codegen_t &cdg, uchar type);

  merror_t emit_float_binary(codegen_t &cdg, uchar type, mcode_t opcode);

  merror_t emit_integer_binary(codegen_t &cdg, uchar type, mcode_t opcode);

  merror_t emit_i64_shift(codegen_t &cdg, mcode_t opcode);

  merror_t emit_i32_shift(codegen_t &cdg, mcode_t opcode);

  merror_t emit_integer_conversion(codegen_t &cdg, uchar input_type, uchar output_type,
                                   mcode_t opcode, int input_width = 0);

  merror_t emit_eqz(codegen_t &cdg, uchar type);

  merror_t emit_compare(codegen_t &cdg, uchar type, mcode_t opcode, bool float_compare = false);

  merror_t emit_float_bits_helper(codegen_t &cdg, const char *helper, int argument_count,
                                  uchar float_type, uchar result_type, bool result_is_float);

  merror_t emit_float_bits_helper(codegen_t &cdg, int argument_count, uchar float_type,
                                  uchar result_type, bool result_is_float);

  merror_t emit_typed_helper(codegen_t &cdg, const char *helper, int argument_count,
                             const uchar *argument_types, uchar result_type);

  merror_t emit_typed_helper_from_instruction(codegen_t &cdg, int argument_count,
                                              const uchar *argument_types, uchar result_type,
                                              bool bits_helper,
                                              const char *helper_prefix = "wasm_");

  merror_t emit_float_conversion(codegen_t &cdg, uchar input_type, uchar output_type,
                                 mcode_t opcode);

  merror_t emit_local(codegen_t &cdg, bool get, bool tee);

  merror_t emit_global(codegen_t &cdg, bool set);

  merror_t emit_ref_null(codegen_t &cdg);

  merror_t emit_ref_is_null(codegen_t &cdg);

  merror_t emit_ref_eq(codegen_t &cdg);

  merror_t emit_ref_func(codegen_t &cdg);

  merror_t emit_ref_as_non_null(codegen_t &cdg);

  merror_t emit_ref_i31(codegen_t &cdg);

  merror_t emit_i31_get(codegen_t &cdg, bool signed_result);

  bool stack_has_prefix(const qvector<wasm_value_t> &prefix) const;

  void add_branch_resume(ea_t source, ea_t target, const wasm_control_frame_t &frame,
                         qvector<wasm_branch_resume_t> *resumes);

  merror_t emit_control_start(codegen_t &cdg, uchar kind);

  merror_t emit_if(codegen_t &cdg);

  merror_t emit_catch(codegen_t &cdg);

  merror_t emit_throw(codegen_t &cdg);

  merror_t emit_else(codegen_t &cdg);

  merror_t emit_end(codegen_t &cdg);

  bool resolve_branch_resume_target(ea_t branch_target, bool has_branch_values,
                                    ea_t *resume_target) const;

  merror_t emit_br(codegen_t &cdg, bool conditional);

  merror_t emit_br_on_reference(codegen_t &cdg, bool branch_on_null);

  merror_t emit_br_table(codegen_t &cdg);

  // PERF: real m_jtbl switch lowering for large tables (below).
  merror_t emit_br_table_m_jtbl(codegen_t &cdg, const qvector<ea_t> &targets,
                                const qvector<bool> &by_serial, const qvector<int> &target_serials,
                                const qvector<mop_t> &direct_targets, mblock_t *head);

  // Jtbl-mode helpers.
  merror_t emit_br_table_jtbl(codegen_t &cdg, const qvector<ea_t> &targets,
                              const qvector<uint32> &depths, const wasm_value_t &selector);

  // emit_branch_edge / emit_branch_goto (the 0xF2/0xFF stub
  // lowerings) and the per-edge spill cache are retired — branch edges
  // lower inline in emit_br / emit_br_on_reference.

  bool emit_sret_store(codegen_t &cdg, const mop_t &value, size_t offset, bool fp);

  // Synthesize one Go result load from the incoming window
  // [SP_local + frame_size + 8 + sp_offset] (lower_control.cpp). Pushes the
  // loaded value with the displayed wasm type.
  merror_t emit_go_result_load(codegen_t &cdg, const loader::go_result_slot_t &slot, uchar type);

  merror_t emit_return(codegen_t &cdg);

  merror_t emit_sparse_indirect_call(codegen_t &cdg);

  merror_t emit_ref_call_dispatch(codegen_t &cdg);

  // Metadata-driven dispatch chains (no stub stream). load reads the
  // packed 'j' case blob at the call source EA; emit builds the shared
  // test/case/default chain for sparse call_indirect and call_ref.
  bool load_dispatch_cases(ea_t source, qvector<uint32> *cases);

  // Memoized existence probe for the same 'j' blob (the per-F5
  // netnode reads the apply() path used to do per call_ref/call_indirect
  // instruction). Result rides the per-function cache.
  bool probe_dispatch_blob(ea_t source);

  merror_t emit_dispatch_chain(codegen_t &cdg, wasm_indirect_dispatch_t &dispatch,
                               ea_t continuation, const qvector<uint32> &cases);

  merror_t emit_memory_trap(codegen_t &cdg);

  // Statically out-of-bounds constant access: the unbounded base+offset sum
  // exceeds the address space or the memory extent, so the access can never
  // succeed (Wasm sums the two unsigned operands without wrapping). Lower it
  // to the shared noreturn trap helper instead of an m_ldx/m_stx from the
  // wrapped (wrong) address.
  merror_t emit_static_memory_trap(codegen_t &cdg);

  merror_t reject_memory64_addressing(codegen_t &cdg);

  merror_t reject_unmapped_memory_extent(codegen_t &cdg);

  // emit_memory_bounds_check (the 0xF9 decode-driven lowering) is
  // retired with the opcode.

  enum class wasm_helper_effect_t
  {
    unknown,
    pure_noside,
    trapping_nomem,
    memory_or_global,
  };

  static bool starts_with(const char *str, const char *prefix);

  static bool ends_with(const char *str, const char *suffix);

  wasm_helper_effect_t classify_typed_helper(const char *helper) const;

  void add_wasm_call_memory_clobber(codegen_t &cdg, ivlset_t *visible_memory,
                                    mlist_t *spoiled) const;

  merror_t emit_call(codegen_t &cdg, const char *typed_helper = nullptr,
                     int typed_helper_arguments = 2, uchar typed_helper_type = 0x7F,
                     bool tail_call = false, const uchar *typed_helper_arg_types = nullptr,
                     bool indirect_call = false, bool no_return = false,
                     bool reference_call = false, bool unwind_call = false,
                     // caller-supplied argument mops for the typed
                     // helper path (length == typed_helper_arguments; a null
                     // entry pops that argument from the abstract stack) —
                     // used by emit_memory_bulk to pass rebased addresses.
                     const mop_t *const *preset_args = nullptr);

  merror_t emit_growable_memory(codegen_t &cdg, const memory_instruction_t &memory,
                                uchar memory_mode);

  // Constant-address lowering for linear-memory accesses. The Wasm spec
  // computes the effective address as the *unbounded* sum of the unsigned
  // address and the memarg offset: for memory32, 0xFFFFFFFF + 1 is
  // 0x100000000, which fails the bounds check and traps; it must never wrap
  // back to a small in-bounds address. memory_extent is a hard upper bound
  // on valid accesses (the exact size for fixed memories, the declared
  // maximum - or the 4-GiB memory32 cap - for growable ones), so an access
  // whose last byte crosses it can never become valid, not even after
  // memory.grow. When *statically_trapping is set the caller must lower the
  // access to a memory trap and must not emit any m_ldx/m_stx.
  bool make_effective_memory_address(codegen_t &cdg, const wasm_value_t &address, int address_size,
                                     uint64 offset_value, uint64 access_width, uint64 memory_extent,
                                     mop_t *effective_address, bool *constant_address,
                                     bool *statically_trapping);

  // A load whose address is the proven 8-byte receiver pointer of a
  // pointer-receiver member access rides an m_ldx over the typed pointer
  // (memarg folded as an 8-byte addend) instead of the flat
  // linear address, so the engine's struct-offset pass renders
  // `a1->field` at non-zero member offsets too. Loads only; base-free by
  // construction (wasmhex_memory_base == 0).
  merror_t emit_go_receiver_member_load(codegen_t &cdg, const struct memory_instruction_t &memory,
                                        const wasm_value_t &address, size_t address_depth);

  merror_t emit_c_frame_scalar_memory(codegen_t &cdg, const memory_instruction_t &memory,
                                      const wasm_value_t &address, const wasm_value_t &value,
                                      size_t address_depth);

  merror_t emit_fixed_memory(codegen_t &cdg, const memory_instruction_t &memory, uchar memory_mode,
                             uint64 memory_bytes);

  merror_t emit_memory(codegen_t &cdg);

  merror_t emit_atomic(codegen_t &cdg);

  merror_t emit_v128_memory(codegen_t &cdg, bool store, int access_width, bool splat = false);

  merror_t emit_v128_lane_memory(codegen_t &cdg);

  merror_t emit_memory_size_grow(codegen_t &cdg, bool grow);

  merror_t emit_table_size(codegen_t &cdg);

  merror_t emit_memory_bulk(codegen_t &cdg, bool copy);

  merror_t emit_memory_init(codegen_t &cdg);

  merror_t emit_data_drop(codegen_t &cdg);

  merror_t emit_unreachable(codegen_t &cdg);

public:
  void clear_for_mba(mba_t *mba);

  void clear_for_shutdown();

  bool match(codegen_t &cdg) override;

  merror_t apply_instruction(codegen_t &cdg);

  merror_t apply(codegen_t &cdg) override;
};

void record_c_arg_string_literal(ea_t entry_ea, ea_t call_ea, size_t arg_index,
                                 const std::string &text, ea_t data_ea);
void record_c_arg_data_address(ea_t entry_ea, ea_t call_ea, size_t arg_index, ea_t item_ea);
void clear_c_arg_records(ea_t entry_ea);

}
