#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <name.hpp>
#include <segment.hpp> // get_segment_info for the zero-init MEMORY range

#include <map>
#include <set>

#include "../../../ida/loader/options.hpp"
#include "../../../backend/hexrays/register_mapping/frame.hpp"
#include "../../../backend/idb/metadata_schema/metadata.hpp"
#include "../../../backend/idb/metadata_schema/netnode_tags.hpp" // tag_t::go_struct_registry latch
#include "../ctree_presentation/ctree_types.hpp"                 // make_go_string_literal
#include "filter.hpp"

namespace
{
// Store-opcode bytes come from the shared single-source opcode table
// not hand-typed hex (0x2C is i32.load8_s, not f32.store).
#define WASMHEX_GEN_GO_BYTE(name, byte, sub, imm, mnem, flags)                                     \
  constexpr uint32 k_go_##name = uint32(byte);
WASMHEX_ITYPES(WASMHEX_GEN_GO_BYTE)
#undef WASMHEX_GEN_GO_BYTE

// When a large Go function has > 1,000 cold panic leaf blocks severed into
// independent 1-armed `if` regions along the function spine (e.g.
// `wasm_func_2422` with 993 cold blocks + 469 inline guards = depth 1,444, and
// `wasm_func_1806` with 998 cold blocks + 925 inline guards = depth 1,923),
// Hex-Rays's structural reducer represents the N sequential `if` statements as
// a right-leaning binary chain of 2-child `type == 1` (block sequence) region
// nodes `[if_1, [if_2, [if_3, ...]]]`. During `CMAT_ZERO -> CMAT_BUILT`,
// `cgen_t::emit_node` recurses down `type == 1` nodes into the same flat
// `cblock_t *` and throws `MERR_COMPLEX (-22)` if the recursion depth reaches
// 1,024 (`0x400`). Flattening nested `type == 1` children into their `type == 1`
// parent at `hxe_structural` when the region tree depth is >= 512 keeps the
// emitted `cblock_t` 100% identical while bounding recursion depth to O(1) for
// sequential `if` chains.
bool &go_structural_flattener_installed()
{
  static bool installed = false;
  return installed;
}

ssize_t idaapi go_structural_hexrays_callback(void *, hexrays_event_t event, va_list va)
{
  if ( event != hxe_structural )
    return 0;
  va_list va_copy_list;
  va_copy(va_copy_list, va);
  void *ct = va_arg(va_copy_list, void *);
  va_end(va_copy_list);
  if ( ct == nullptr )
    return 0;

  const int entry = *reinterpret_cast<const int *>(static_cast<const char *>(ct) + 0x24);
  auto *succs = reinterpret_cast<qvector<intvec_t> *>(static_cast<char *>(ct) + 0x30);
  const auto *node_types
    = reinterpret_cast<const intvec_t *>(static_cast<const char *>(ct) + 0x130);
  const size_t n = succs->size();
  if ( n == 0 || node_types->size() != n || entry < 0 || size_t(entry) >= n )
    return 0;

  struct dfs_frame_t
  {
    int node;
    int depth;
    bool expanded;
  };
  std::vector<uint8_t> visited(n, 0);
  std::vector<int> postorder;
  postorder.reserve(n);
  std::vector<dfs_frame_t> work;
  work.reserve(n);
  work.push_back({ entry, 1, false });
  int max_depth = 0;

  while ( !work.empty() )
  {
    const dfs_frame_t cur = work.back();
    work.pop_back();
    if ( cur.node < 0 || size_t(cur.node) >= n )
      return 0;
    if ( cur.expanded )
    {
      postorder.push_back(cur.node);
      continue;
    }
    if ( visited[size_t(cur.node)] != 0 )
      continue;
    visited[size_t(cur.node)] = 1;
    if ( cur.depth > max_depth )
      max_depth = cur.depth;
    const int t = (*node_types)[size_t(cur.node)];
    if ( t < 0 || t > 20 )
      return 0;
    work.push_back({ cur.node, cur.depth, true });
    if ( t != 0 )
    {
      const intvec_t &kids = (*succs)[size_t(cur.node)];
      for ( size_t i = kids.size(); i > 0; --i )
      {
        const int child = kids[i - 1];
        if ( child < 0 || size_t(child) >= n )
          return 0;
        if ( visited[size_t(child)] == 0 )
          work.push_back({ child, cur.depth + 1, false });
      }
    }
  }

  if ( max_depth < 512 )
    return 0;

  for ( int u : postorder )
  {
    if ( (*node_types)[size_t(u)] != 1 )
      continue;
    intvec_t &kids = (*succs)[size_t(u)];
    bool has_seq_child = false;
    size_t flat_count = 0;
    for ( size_t i = 0; i < kids.size(); ++i )
    {
      const int c = kids[i];
      if ( (*node_types)[size_t(c)] == 1 )
      {
        has_seq_child = true;
        flat_count += (*succs)[size_t(c)].size();
      }
      else
      {
        ++flat_count;
      }
    }
    if ( !has_seq_child )
      continue;
    intvec_t flattened;
    flattened.reserve(flat_count);
    for ( size_t i = 0; i < kids.size(); ++i )
    {
      const int c = kids[i];
      if ( (*node_types)[size_t(c)] == 1 )
      {
        const intvec_t &ckids = (*succs)[size_t(c)];
        for ( size_t j = 0; j < ckids.size(); ++j )
          flattened.push_back(ckids[j]);
      }
      else
      {
        flattened.push_back(c);
      }
    }
    kids.swap(flattened);
  }
  return 0;
}

void ensure_go_structural_flattener_installed()
{
  if ( !go_structural_flattener_installed() && get_hexdsp() != nullptr )
  {
    go_structural_flattener_installed()
      = install_hexrays_callback(go_structural_hexrays_callback, nullptr);
  }
}

void remove_go_structural_flattener()
{
  if ( go_structural_flattener_installed() && get_hexdsp() != nullptr )
  {
    remove_hexrays_callback(go_structural_hexrays_callback, nullptr);
    go_structural_flattener_installed() = false;
  }
}

// The processor-side return-synthesis build marker (this TU's
// reset_state owns the Go result-slot plumbing; verified via strings on the
// deployed procs dylib).
extern "C" __attribute__((used)) const char kWasmhexG5S2bFilterMarker[]
  = "WASMHEX_BUILD_MARKER_G5_RESULT_SYNTHESIS";
// 3.7: per-IDB named-struct registry latch (netnode 'v', schema-word) —
// same verification path: strings on the deployed procs dylib.
extern "C" __attribute__((used)) const char kWasmhexM0bPerIdbRegistryMarker[]
  = "WASMHEX_BUILD_MARKER_M0B_PER_IDB_STRUCT_REGISTRY";
// IRTIME — per-function wall time of reset_state's collection phase, split
// into the two decode families (WASMHEX_DEBUG=irtime, one line per
// microcode batch):
//   blob  — module-level metadata acquisition (blob 't' fetch+decode on
//            every collector; becomes the identity-checked ModuleIndex hit)
//   body  — linear body re-decodes inside the collectors (becomes the
//            cached FunctionIR instruction stream)
// The *_builds counters are PROCESS-cumulative and come from the cache
// accessors (go_module_index_builds / go_function_ir_builds): a sweep
// shows N per F5 before, 1 per identity after. The ms values are per
// reset_state call.
namespace irtime
{
double &blob_ms_total()
{
  static double v = 0.0;
  return v;
}
double &body_ms_total()
{
  static double v = 0.0;
  return v;
}
bool on()
{
  return wasmhex::options::debug("irtime");
}
struct scope_t
{
  enum class kind_t
  {
    blob,
    body,
  };
  std::chrono::steady_clock::time_point t0;
  double *total = nullptr;
  bool active = false;
  scope_t(kind_t kind)
  {
    if ( !on() )
      return;
    active = true;
    t0 = std::chrono::steady_clock::now();
    total = kind == kind_t::blob ? &blob_ms_total() : &body_ms_total();
  }
  ~scope_t()
  {
    if ( active )
      *total
        += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  }
};
} // namespace irtime
} // namespace

namespace wasmhex
{
void wasmhex_filter_t::clear_runtime_state()
{
  state_ready = false;
  state_mba = nullptr;
  state_entry = BADADDR;
  current_insn_ea = BADADDR;
  function_type.clear();
  function_type.rettype.clear();
  function_type.retloc = argloc_t();
  function_type.spoiled.clear();
  stack.clear();
  locals.clear();
  controls.clear();
  resume_states.clear();
  indirect_dispatches.clear();
  operand_register_index.clear();
  scratch_register_index.clear();
  branch_table_selector = mop_t();
  function_params.clear();
  function_results.clear();
  declared_local_types.clear();
  sret_pointer_reg = mr_none;
  boolean_scratch_reg = mr_none;
  bounds_mode = wasmhex_bounds_mode_t::explicit_bounds;
  c_frame_metadata = wasmhex_c_frame_metadata_t();
  has_c_frame = false;
  c_frame_fp_written = false;
  c_frame_sp_read = false;
  has_sret_pointer = false;
  unreachable = false;
  function_end_emitted = false;
  soft_c_frame = false;
  soft_frame_learned = false;
  go_module_recognized = false;
  soft_sp_index = UINT32_MAX;
  go_frame_mode = false;
  go_frame_leg_eas.clear();
  go_receiver_wrap_eas.clear();
  active_function_ir = nullptr; // per-function cache pointer
  logged_unknown_helper = false;
  go_results_synthesis = false;
  go_result_slots.clear();
  go_call_shapes.clear();
  go_empty_args_store_eas.clear();
  go_window_fold_store_eas.clear();
  go_call_group_stores.clear();
}

bool wasmhex_filter_t::collect_go_call_sp_arith(const qvector<ea_t> &store_eas,
                                                std::set<ea_t> *sp_arith_eas,
                                                std::map<ea_t, uint32> *sp_cache_local_by_store)
{
  if ( store_eas.empty() )
    return true;
  // The owner probe stays as the function-existence gate (the walk
  // itself reads the cached stream; bounds live in the stream records).
  func_entry_info_t owner;
  if ( !get_func_entry_info(&owner, state_entry) )
    return false;
  qvector<ea_t> sorted = store_eas;
  std::sort(sorted.begin(), sorted.end());
  struct decoded_insn_t
  {
    ea_t ea;
    int itype;
    uint32 op1;
  };
  decoded_insn_t window[7] = {};
  size_t window_size = 0;
  size_t next_store = 0;
  // The walk reads the decode-once stream (same shape checks,
  // same op1 truncation to uint32 the decoded window applied).
  const loader::function_ir_t *fir = active_function_ir;
  if ( fir == nullptr || !fir->stream_valid )
    return false;
  for ( size_t i = 0; i < fir->insns.size(); ++i )
  {
    const ea_t ea = ea_t(fir->insns[i].ea);
    if ( next_store < sorted.size() && ea == sorted[next_store] )
    {
      // SP cache local L (three reads agree), SP global 0 write, and the
      // i64.const store value right before the recorded store.
      const bool shape_ok = window_size == 7 && window[0].itype == wasmhex_local_get
        && window[1].itype == wasmhex_i32_const && window[2].itype == wasmhex_i32_sub
        && window[3].itype == wasmhex_local_tee && window[4].itype == wasmhex_global_set
        && window[4].op1 == 0 && window[5].itype == wasmhex_local_get
        && window[6].itype == wasmhex_i64_const && window[0].op1 == window[3].op1
        && window[0].op1 == window[5].op1;
      if ( !shape_ok )
        return false;
      for ( size_t k = 0; k < 5; ++k )
        sp_arith_eas->insert(window[k].ea);
      if ( sp_cache_local_by_store != nullptr )
        (*sp_cache_local_by_store)[ea] = window[0].op1;
      ++next_store;
    }
    if ( window_size < 7 )
      ++window_size;
    else
      memmove(window, window + 1, sizeof(window) - sizeof(window[0]));
    window[window_size - 1]
      = decoded_insn_t{ ea, int(fir->insns[i].itype), uint32(fir->insns[i].op1) };
  }
  return next_store == sorted.size();
}

// The Go frame legs — prologue (SP -= frame_size, cached in the
// SP-cache local L) and epilogues (SP += frame_size + 8, popping the
// pushed return-address slot). Verified by the exact five-instruction
// shapes on the decoded stream, the same discipline as
// collect_go_call_sp_arith; the first matching prologue fixes L (the
// frame anchor local) and every epilogue must agree on L.
bool wasmhex_filter_t::collect_go_frame_legs(const loader::go_function_ir_t &go_ir)
{
  go_frame_leg_eas.clear();
  go_receiver_wrap_eas.clear();
  func_entry_info_t owner;
  if ( !get_func_entry_info(&owner, state_entry) )
    return false;
  uint32 prologue_local = UINT32_MAX;
  uint32 epilogue_local = UINT32_MAX;
  struct decoded_insn_t
  {
    ea_t ea;
    int itype;
    uint32 op1;
  };
  decoded_insn_t window[5] = {};
  size_t window_size = 0;
  // The leg walk reads the decode-once stream (identical window
  // semantics — op1 truncated to uint32 exactly as the decoded form did).
  const loader::function_ir_t *fir = active_function_ir;
  if ( fir == nullptr || !fir->stream_valid )
    return false;
  for ( size_t i = 0; i < fir->insns.size(); ++i )
  {
    const ea_t ea = ea_t(fir->insns[i].ea);
    const bool window_full = window_size == 5;
    if ( window_full )
    {
      // prologue: local.get L; i32.const K; i32.sub; local.tee L; global.set 0
      const bool prologue = window[0].itype == wasmhex_local_get
        && window[1].itype == wasmhex_i32_const && window[2].itype == wasmhex_i32_sub
        && window[3].itype == wasmhex_local_tee && window[4].itype == wasmhex_global_set
        && window[4].op1 == 0 && window[0].op1 == window[3].op1
        && window[1].op1 == go_ir.frame_size;
      // epilogue: local.get L; i32.const K; i32.add; local.tee L; global.set 0
      // (go1.27 emits TWO legs at return: SP += frame_size, then SP += 8
      // popping the return-address slot — a single fs+8 leg is accepted
      // too; the engine folds the pair into one display add).
      const bool epilogue = window[0].itype == wasmhex_local_get
        && window[1].itype == wasmhex_i32_const && window[2].itype == wasmhex_i32_add
        && window[3].itype == wasmhex_local_tee && window[4].itype == wasmhex_global_set
        && window[4].op1 == 0 && window[0].op1 == window[3].op1
        && (window[1].op1 == go_ir.frame_size || window[1].op1 == go_ir.frame_size + 8u
            || window[1].op1 == 8u);
      if ( window_full && !prologue && !epilogue && window[4].itype == wasmhex_global_set
           && window[4].op1 == 0 && window[0].itype == wasmhex_local_get
           && window[3].itype == wasmhex_local_tee && window[0].op1 == window[3].op1
           && (window[2].itype == wasmhex_i32_add || window[2].itype == wasmhex_i32_sub)
           && window[1].itype == wasmhex_i32_const && options::debug("mc") )
      {
        char buf[160];
        int n = qsnprintf(buf, sizeof(buf), "M1D2_NEARLEG ea=%a %s local=%u k=%u fs=%u\n",
                          window[0].ea, window[2].itype == wasmhex_i32_add ? "add" : "sub",
                          window[0].op1, window[1].op1, go_ir.frame_size);
        if ( n > 0 )
          ::write(2, buf, size_t(n));
      }
      if ( prologue || epilogue )
      {
        const uint32 leg_local = window[0].op1;
        if ( options::debug("mc") )
        {
          char buf[160];
          int n = qsnprintf(buf, sizeof(buf), "M1D2_LEG ea=%a %s local=%u k=%u fs=%u\n",
                            window[0].ea, prologue ? "prologue" : "epilogue", leg_local,
                            window[1].op1, go_ir.frame_size);
          if ( n > 0 )
            ::write(2, buf, size_t(n));
        }
        if ( prologue )
        {
          if ( prologue_local != UINT32_MAX )
            return false; // two prologues: not the fixed-frame shape
          prologue_local = leg_local;
        }
        else
        {
          if ( epilogue_local == UINT32_MAX )
            epilogue_local = leg_local;
          else if ( epilogue_local != leg_local )
            return false;
        }
        for ( size_t k = 0; k < 5; ++k )
          go_frame_leg_eas.insert(window[k].ea);
      }
    }
    if ( !window_full )
      ++window_size;
    else
      memmove(window, window + 1, sizeof(window) - sizeof(window[0]));
    window[window_size - 1]
      = decoded_insn_t{ ea, int(fir->insns[i].itype), uint32(fir->insns[i].op1) };
  }
  if ( prologue_local == UINT32_MAX )
    return false;
  if ( epilogue_local != UINT32_MAX && epilogue_local != prologue_local )
    return false;
  c_frame_metadata.fp_local_index = prologue_local;
  return true;
}

bool wasmhex_filter_t::collect_go_call_sp_refresh_sets(
  const qvector<ea_t> &refresh_eas, std::map<ea_t, std::pair<ea_t, uint32>> *refresh_info)
{
  if ( refresh_eas.empty() )
    return true;
  // The owner probe stays as the function-existence gate (the walk
  // itself reads the cached stream; bounds live in the stream records).
  func_entry_info_t owner;
  if ( !get_func_entry_info(&owner, state_entry) )
    return false;
  qvector<ea_t> sorted = refresh_eas;
  std::sort(sorted.begin(), sorted.end());
  size_t next_refresh = 0;
  // Linear walk over the decode-once stream — the record after
  // the `global.get 0` is exactly the `ea + insn.size` the single decode
  // used to compute.
  const loader::function_ir_t *fir = active_function_ir;
  if ( fir == nullptr || !fir->stream_valid )
    return false;
  const std::vector<loader::go_insn_record_t> &insns = fir->insns;
  for ( size_t i = 0; i < insns.size() && next_refresh < sorted.size(); ++i )
  {
    const ea_t ea = ea_t(insns[i].ea);
    if ( ea > sorted[next_refresh] )
      return false; // the recorded refresh EA is not on the decoded stream
    if ( ea == sorted[next_refresh] )
    {
      if ( insns[i].itype != wasmhex_global_get || uint32(insns[i].op1) != 0 )
        return false;
      if ( i + 1 >= insns.size() || insns[i + 1].itype != wasmhex_local_set )
        return false;
      const ea_t set_ea = ea_t(insns[i + 1].ea);
      (*refresh_info)[ea] = { set_ea, uint32(insns[i + 1].op1) };
      ++next_refresh;
      i += 1; // continue the linear walk at the local.set
      continue;
    }
  }
  return next_refresh == sorted.size();
}

void wasmhex_filter_t::collect_go_call_shapes(netnode &wasm_node,
                                              const loader::go_module_index_t &index,
                                              const loader::go_function_ir_t &go_ir)
{
  go_call_shapes.clear();
  go_empty_args_store_eas.clear();
  go_window_fold_store_eas.clear();
  go_call_group_stores.clear();
  if ( !go_ir.present )
    return;
  if ( wasm_node == BADNODE || !index.layer_present )
    return;
  const loader::go_type_info_t &layer = index.layer;
  {
    constexpr uint32 kGoStructRegistrySchema = 1;
    uint32 registered_schema = 0;
    const ssize_t stored = wasm_node.supval(0, &registered_schema, sizeof(registered_schema),
                                            static_cast<uchar>(wasmhex::tag_t::go_struct_registry));
    if ( stored != sizeof(registered_schema) || registered_schema != kGoStructRegistrySchema )
    {
      for ( const loader::go_type_descriptor_t &descriptor : layer.descriptors )
      {
        if ( descriptor.kind != 25 || descriptor.size == 0 || descriptor.size > 128 )
          continue;
        const std::string c_name = loader::go_descriptor_c_name(descriptor);
        if ( c_name.empty() || c_name == "go__string" )
          continue; // canonical layout registers on first use
        std::vector<loader::go_c_field_t> fields;
        std::uint64_t size = 0;
        if ( loader::go_descriptor_struct_fields(layer, descriptor, &fields, &size) )
          ensure_go_struct_type(c_name.c_str(), fields, size); // memoized inside
      }
      wasm_node.supset(0, &kGoStructRegistrySchema, sizeof(kGoStructRegistrySchema),
                       static_cast<uchar>(wasmhex::tag_t::go_struct_registry));
    }
  }
  if ( !layer.present || layer.arginfo.empty() )
    return;
  // The number of function imports (smallest live 'F' key, probed ONCE per
  // IDB by the ModuleIndex): defined bodies were easet_idx'ed at
  // function_index = imports + i ('F' entries exist only for defined
  // functions). UINT32_MAX = the probe found no defined function.
  const uint32 import_count = index.import_count;
  if ( import_count == UINT32_MAX )
    return;
  func_entry_info_t owner;
  if ( !get_func_entry_info(&owner, state_entry) )
    return;
  // One linear decode of the body.
  struct window_insn_t
  {
    ea_t ea;
    int itype;
    uint64 op1;
  };
  // The body was decoded ONCE (FunctionIR); this collector
  // materializes its local view from the stream records — same fields
  // {ea, itype, Op1.value}, same fail-closed on a failed linear decode.
  if ( active_function_ir == nullptr || !active_function_ir->stream_valid )
    return;
  qvector<window_insn_t> insns;
  insns.reserve(active_function_ir->insns.size());
  for ( const loader::go_insn_record_t &record : active_function_ir->insns )
    insns.push_back(window_insn_t{ ea_t(record.ea), int(record.itype), record.op1 });
  const auto store_width = [](int itype) -> int
  {
    if ( itype == wasmhex_i64_store || itype == wasmhex_f64_store )
      return 8;
    if ( itype == wasmhex_i32_store || itype == wasmhex_f32_store )
      return 4;
    return 0;
  };
  // The wasm value type of a window store BY OPCODE — the recorded
  // args only cover a subset of the window and slot widths cannot tell
  // f64 from i64 (or f32 from i32).
  const auto store_value_type = [](int itype) -> uchar
  {
    if ( itype == wasmhex_i64_store )
      return 0x7E;
    if ( itype == wasmhex_f64_store )
      return 0x7C;
    if ( itype == wasmhex_f32_store )
      return 0x7D;
    if ( itype == wasmhex_i32_store )
      return 0x7F;
    return 0;
  };
  const auto is_address_quintet = [&](size_t j) -> bool
  {
    // insns[j .. j+4] == local.get 1; i64.extend_i32_u; i64.const K;
    // i64.add; i32.wrap_i64 — the standard computed window address. The
    // j >= 5 guard is the caller's size_t discipline (j - 5 below must
    // never underflow).
    return j + 4 < insns.size() && insns[j].itype == wasmhex_local_get && insns[j].op1 == 1
      && insns[j + 1].itype == wasmhex_i64_extend_i32_u && insns[j + 2].itype == wasmhex_i64_const
      && insns[j + 3].itype == wasmhex_i64_add && insns[j + 4].itype == wasmhex_i32_wrap_i64;
  };
  const auto quintet_at = [&](size_t j) -> bool
  {
    return j >= 5 && is_address_quintet(j - 5);
  };
  // The return-address pushes (`SP -= 8; [SP] = RESUME_PC`) are
  // call plumbing the apply side already consumes — they must not count as
  // window touches in the single-use audit (every call carries one right
  // before its call).
  std::set<ea_t> ret_push_eas;
  for ( const loader::go_call_ir_t &ret_call : go_ir.calls )
    if ( ret_call.store_offset != UINT32_MAX )
      ret_push_eas.insert(state_entry + ret_call.store_offset);
  for ( const loader::go_call_ir_t &call : go_ir.calls )
  {
    if ( call.is_pc_f_fast_return || call.fn_index == UINT32_MAX || call.fn_index < import_count )
      continue;
    const uint32 defined_index = call.fn_index - import_count;
    if ( defined_index >= layer.arginfo.size() )
      continue;
    const loader::go_arginfo_t &arginfo = layer.arginfo[defined_index];
    if ( !arginfo.present || arginfo.slots.empty()
         || arginfo.group_starts.size() != arginfo.slots.size() )
      continue;
    bool has_group = false;
    for ( uint8 flag : arginfo.group_starts )
      if ( flag != 0 )
      {
        has_group = true;
        break;
      }
    if ( !has_group )
      continue; // scalars only: nothing to merge
    // Param layout from the slots + group flags: a flagged slot opens a
    // group spanning consecutive depth>=1 slots; unflagged depth-0 slots
    // are scalar parameters.
    qvector<go_call_param_t> params;
    bool layout_ok = true;
    {
      size_t k = 0;
      while ( k < arginfo.slots.size() && layout_ok )
      {
        const loader::go_arginfo_slot_t &slot = arginfo.slots[k];
        if ( slot.depth == 0 )
        {
          if ( arginfo.group_starts[k] != 0 )
          {
            layout_ok = false; // a flagged scalar: impossible encoding
            break;
          }
          go_call_param_t param;
          param.is_group = false;
          param.offset = slot.offset;
          param.bytes = slot.size;
          param.type = slot.size == 8 ? uchar(0x7E) : uchar(0x7F);
          params.push_back(param);
          ++k;
          continue;
        }
        if ( arginfo.group_starts[k] == 0 )
        {
          layout_ok = false; // depth>=1 outside a group: cannot happen
          break;
        }
        // The group spans until the next flag or the first depth-0 slot.
        size_t end = k + 1;
        while ( end < arginfo.slots.size() && arginfo.slots[end].depth >= 1
                && arginfo.group_starts[end] == 0 )
          ++end;
        const uint32 group_offset = arginfo.slots[k].offset;
        const uint32 group_extent
          = arginfo.slots[end - 1].offset + arginfo.slots[end - 1].size - group_offset;
        go_call_param_t param;
        if ( group_extent <= 8 )
        {
          // A one-word aggregate renders exactly as its word; a pointer
          // argument would lose the value. Demote to a scalar parameter.
          param.is_group = false;
          param.offset = arginfo.slots[k].offset;
          param.bytes = arginfo.slots[k].size;
          param.type = 0;
        }
        else
        {
          param.is_group = true;
          param.offset = group_offset;
          param.bytes = group_extent;
          param.type = 0x7F;
        }
        params.push_back(param);
        k = end;
      }
    }
    if ( !layout_ok )
      continue;
    bool any_merge = false;
    for ( const go_call_param_t &param : params )
      if ( param.is_group )
      {
        any_merge = true;
        // The expressible layouts: 2 words (string/iface/struct16) and 3
        // words (slice/struct24). Bigger aggregates stay on the word
        // display — fail-closed (their C pointer type would need a
        // per-size virtual struct with no added information over the
        // visible stores).
        if ( param.bytes > 24 )
          any_merge = false;
      }
    if ( !any_merge )
      continue;
    // Slot tiling sanity: every slot must sit exactly at the running cursor.
    {
      uint32 cursor = 0;
      for ( const loader::go_arginfo_slot_t &slot : arginfo.slots )
      {
        if ( slot.offset != cursor )
        {
          cursor = UINT32_MAX;
          break;
        }
        cursor += slot.size;
      }
      if ( cursor == UINT32_MAX )
        continue;
    }
    // Locate the call's SP-decrement anchor (sp_arith_offsets[0]) in the
    // decoded vector; the outgoing-window stores are the contiguous run
    // immediately before it.
    const ea_t sp_anchor = state_entry + call.sp_arith_offsets[0];
    size_t sp_index = 0;
    bool found_sp = false;
    for ( size_t k = insns.size(); k != 0; --k )
      if ( insns[k - 1].ea == sp_anchor )
      {
        sp_index = k - 1;
        found_sp = true;
        break;
      }
    if ( !found_sp || sp_index == 0 )
      continue;
    // Match the window stores top-down against the slots (descending).
    struct recovered_t
    {
      ea_t ea;
      uint32 offset;
      int width;
      uchar type;
    };
    qvector<recovered_t> recovered;
    bool window_ok = true;
    {
      size_t k = sp_index; // one past the last window insn
      for ( size_t s = arginfo.slots.size(); s != 0 && window_ok; --s )
      {
        const loader::go_arginfo_slot_t &slot = arginfo.slots[s - 1];
        if ( k == 0 )
        {
          window_ok = false;
          break;
        }
        const window_insn_t &store = insns[k - 1];
        const int width = store_width(store.itype);
        if ( width == 0 || width != int(slot.size) )
        {
          window_ok = false;
          break;
        }
        const uint64 memarg = store.op1;
        size_t j = k - 1;
        bool matched = false;
        // The statement run between the previous match and this store is
        // [address][value...]. Address forms: A = the quintet
        // `local.get 1; i64.extend_i32_u; i64.const K; i64.add;
        // i32.wrap_i64` (memarg 0, window offset K) and B = a bare
        // `local.get 1` (window offset in the memarg). Values may embed
        // their own SP+k computations — the same shapes minus (form A) or
        // including (4-insn variant) the trailing wrap — and are skipped.
        const auto skip_sp_prefix = [&](size_t at) -> int
        {
          // `at` indexes a `local.get 1`; returns the prefix length
          // (4 without the wrap, 5 with) or 0 when not an SP computation.
          if ( at + 1 >= insns.size() || insns[at + 1].itype != wasmhex_i64_extend_i32_u )
            return 0;
          if ( at + 3 >= insns.size() || insns[at + 2].itype != wasmhex_i64_const
               || insns[at + 3].itype != wasmhex_i64_add )
            return 0;
          if ( at + 4 < insns.size() && insns[at + 4].itype == wasmhex_i32_wrap_i64 )
            return 5;
          return 4;
        };
        while ( j != 0 )
        {
          if ( quintet_at(j) && insns[j - 3].op1 == slot.offset && memarg == 0 )
          {
            j -= 5; // form A address quintet
            matched = true;
            break;
          }
          if ( insns[j - 1].itype == wasmhex_local_get && insns[j - 1].op1 == 1 )
          {
            const int prefix = skip_sp_prefix(j - 1);
            if ( prefix != 0 )
            {
              // An SP+k VALUE computation: only its lowest insn is below
              // the scan point — step past it (the rest sits above and is
              // never revisited; the scan only moves down).
              (void)prefix;
              --j;
              continue;
            }
            if ( memarg != 0 )
            {
              if ( memarg == slot.offset )
              {
                --j; // form B bare base
                matched = true;
              }
              else
                window_ok = false;
              break;
            }
            if ( slot.offset == 0 )
            {
              --j; // bare base with memarg 0 matches offset 0
              matched = true;
            }
            else
              window_ok = false;
            break;
          }
          --j;
        }
        if ( !matched )
        {
          window_ok = false;
          break;
        }
        recovered.push_back(
          recovered_t{ store.ea, slot.offset, width, store_value_type(store.itype) });
        k = j;
      }
      if ( recovered.size() != arginfo.slots.size() )
        window_ok = false;
    }
    if ( !window_ok )
      continue;
    // Scalar parameter types come from the REAL store opcodes (the ArgInfo
    // slot width cannot distinguish f64/f32 words).
    {
      bool types_ok = true;
      for ( go_call_param_t &param : params )
      {
        if ( param.is_group )
          continue;
        for ( const recovered_t &store : recovered )
          if ( store.offset == param.offset )
          {
            param.type = store.type;
            break;
          }
        if ( param.type == 0 )
          types_ok = false;
      }
      if ( !types_ok )
        continue;
    }
    std::reverse(recovered.begin(), recovered.end());
    go_call_shape_t shape;
    shape.params = params;
    {
      const loader::go_func_layout_t &layout = defined_index < layer.layouts.size()
        ? layer.layouts[defined_index]
        : loader::go_func_layout_t();
      const bool layout_matches = layout.source != loader::go_layout_source_t::kNone
        && layout.params.size() == shape.params.size();
      // Canonical names by extent first; the record
      // names refine the 2-word (string / iface / named struct) and 3-word
      // (slice / named struct) groups the extent cannot distinguish alone.
      for ( size_t p = 0; p < shape.params.size(); ++p )
      {
        go_call_param_t &param = shape.params[p];
        if ( !param.is_group )
          continue;
        param.group_type.sprnt("go_agg%u", unsigned(param.bytes));
        if ( param.bytes == 24 )
          param.group_type = "go_slice";
      }
      if ( layout_matches )
      {
        for ( size_t p = 0; p < shape.params.size(); ++p )
        {
          const loader::go_layout_param_t &record = layout.params[p];
          go_call_param_t &param = shape.params[p];
          if ( !param.is_group || record.offset != param.offset || record.bytes != param.bytes
               || record.c_name.empty() )
            continue;
          // The same spelling mapping the agreement scan applied (the
          // canonical layouts; a NAMED struct keeps its go__X name and
          // carries the field list for the emit-side til registration).
          if ( record.c_name == "go__string" )
            param.group_type = "go_string";
          else if ( record.c_name == "go__iface" )
            param.group_type = "go_iface";
          else if ( record.c_name == "go__slice" )
            param.group_type = "go_slice";
          else if ( !record.fields.empty() )
          {
            param.group_type = record.c_name.c_str();
            param.group_fields = record.fields;
          }
        }
      }
    }
    // Commit. Group window stores leave the value-keeping consume set
    // (their words render as the real stores they are; the group's single
    // pointer argument is synthesized at the call). The erasures are held
    // locally until every gate below passes — a rejected call keeps the
    // exact display.
    std::set<ea_t> group_window_stores;
    std::set<ea_t> scalar_window_stores;
    size_t slot_cursor = 0;
    bool any_committed = false;
    for ( go_call_param_t &param : shape.params )
    {
      if ( !param.is_group )
      {
        if ( slot_cursor < recovered.size() )
        {
          scalar_window_stores.insert(recovered[slot_cursor].ea);
          param.window_store_eas.push_back(recovered[slot_cursor].ea);
        }
        ++slot_cursor; // a scalar parameter is exactly one slot
        continue;
      }
      size_t slots_in_group = 0;
      uint32 covered = 0;
      while ( covered < param.bytes && slot_cursor + slots_in_group < recovered.size() )
      {
        covered += recovered[slot_cursor + slots_in_group].width;
        ++slots_in_group;
      }
      if ( covered != param.bytes )
      {
        slot_cursor += slots_in_group;
        continue;
      }
      for ( size_t g = 0; g < slots_in_group; ++g )
      {
        group_window_stores.insert(recovered[slot_cursor + g].ea);
        param.window_store_eas.push_back(recovered[slot_cursor + g].ea);
        any_committed = true;
      }
      slot_cursor += slots_in_group;
    }
    if ( !any_committed )
      continue;
    std::set<ea_t> recorded_stores;
    for ( const loader::go_arg_ir_t &recorded : call.args )
      recorded_stores.insert(state_entry + recorded.store_offset);
    std::set<ea_t> window_stores;
    for ( const recovered_t &store : recovered )
      window_stores.insert(store.ea);
    for ( const ea_t scalar_store : scalar_window_stores )
      if ( recorded_stores.find(scalar_store) == recorded_stores.end() )
      {
        any_committed = false;
        break;
      }
    for ( const ea_t recorded_store : recorded_stores )
      if ( window_stores.find(recorded_store) == window_stores.end() )
      {
        any_committed = false;
        break;
      }
    if ( !any_committed )
      continue;
    shape.sp_local = call.sp_cache_local;
    // Prove an EMPTY variadic []any — the callee's layout record
    // is variadic, the last parameter is the 24-byte slice group, and
    // every one of its slots stores a constant zero (`i64.const 0` right
    // before the store). Fail-closed on any other value shape.
    if ( !shape.params.empty() && defined_index < index.layer.layouts.size()
         && index.layer.layouts[defined_index].variadic )
    {
      const go_call_param_t &last = shape.params.back();
      if ( last.is_group && last.bytes == 24 && last.window_store_eas.size() == 3 )
      {
        bool all_zero = true;
        for ( const ea_t store_ea : last.window_store_eas )
        {
          // Locate the store in the decoded stream; the value producer is
          // the instruction right before it.
          size_t at = insns.size();
          for ( size_t q = 0; q < insns.size(); ++q )
            if ( insns[q].ea == store_ea )
            {
              at = q;
              break;
            }
          if ( at == 0 || at == insns.size() || insns[at].itype != wasmhex_i64_store
               || insns[at - 1].itype != wasmhex_i64_const || insns[at - 1].op1 != 0 )
          {
            all_zero = false;
            break;
          }
        }
        if ( all_zero )
        {
          shape.empty_variadic_args = true;
          for ( const ea_t store_ea : last.window_store_eas )
          {
            shape.empty_args_store_eas.push_back(store_ea);
            go_empty_args_store_eas.insert(store_ea);
          }
        }
      }
    }
    {
      const auto insn_at = [&](ea_t ea) -> size_t
      {
        for ( size_t q = 0; q < insns.size(); ++q )
          if ( insns[q].ea == ea )
            return q;
        return SIZE_MAX;
      };
      const auto width_of = [](int itype) -> int
      {
        if ( itype == wasmhex_i64_load || itype == wasmhex_f64_load || itype == wasmhex_i64_store
             || itype == wasmhex_f64_store )
          return 8;
        if ( itype == wasmhex_i32_load || itype == wasmhex_f32_load || itype == wasmhex_i32_store
             || itype == wasmhex_f32_store )
          return 4;
        return 0;
      };
      // Window accesses and escapes for THIS call's SP-cache local: an
      // access is `[local.get L; i64.extend; i64.const K; i64.add; wrap] +
      // memarg` (form A, window offset K+memarg) or `local.get L` + memarg
      // (form B, window offset = memarg). A quintet that does NOT feed a
      // memory access escapes its offset (the address leaves as a value).
      struct window_access_t
      {
        size_t at;
        uint32 offset;
        int width;
        bool store;
      };
      qvector<window_access_t> accesses;
      std::vector<std::pair<uint32, size_t>> escaped;
      const uint32 sp_local = call.sp_cache_local;
      for ( size_t q = 0; q < insns.size(); ++q )
      {
        // Address forms feeding a memory op. A LOAD takes its address
        // directly: [addr][load]. A STORE has the value between: [addr]
        // [value][store]. Form A = the quintet `local.get L; i64.extend;
        // i64.const K; i64.add; wrap` (window offset K + memarg); form B =
        // a bare `local.get L` (window offset = memarg). A quintet with no
        // memory consumer within two instructions escapes its offset (the
        // address leaves as a value).
        const auto is_store_op = [&](int itype) -> bool
        {
          return itype == wasmhex_i64_store || itype == wasmhex_i32_store
            || itype == wasmhex_f64_store || itype == wasmhex_f32_store;
        };
        const auto is_load_op = [&](int itype) -> bool
        {
          return itype == wasmhex_i64_load || itype == wasmhex_i32_load || itype == wasmhex_f64_load
            || itype == wasmhex_f32_load;
        };
        const auto is_value_push = [&](int itype) -> bool
        {
          return itype == wasmhex_i64_const || itype == wasmhex_i32_const
            || itype == wasmhex_f64_const || itype == wasmhex_f32_const
            || itype == wasmhex_local_get || itype == wasmhex_global_get;
        };
        const auto record = [&](size_t at, uint32 offset) -> void
        {
          if ( ret_push_eas.count(insns[at].ea) != 0 )
            return; // return-address push: consumed plumbing, not a window touch
          accesses.push_back(window_access_t{ at, offset + uint32(insns[at].op1),
                                              width_of(insns[at].itype),
                                              is_store_op(insns[at].itype) });
        };
        if ( q + 4 < insns.size() && insns[q].itype == wasmhex_local_get
             && uint32(insns[q].op1) == sp_local && insns[q + 1].itype == wasmhex_i64_extend_i32_u
             && insns[q + 2].itype == wasmhex_i64_const && insns[q + 3].itype == wasmhex_i64_add
             && insns[q + 4].itype == wasmhex_i32_wrap_i64 )
        {
          const uint32 base_offset = uint32(insns[q + 2].op1);
          if ( q + 5 < insns.size() && is_load_op(insns[q + 5].itype) )
          {
            record(q + 5, base_offset);
            continue;
          }
          if ( q + 6 < insns.size() && is_value_push(insns[q + 5].itype)
               && is_store_op(insns[q + 6].itype) )
          {
            record(q + 6, base_offset);
            continue;
          }
          escaped.push_back(std::make_pair(base_offset, q)); // the address escapes as a value
          continue;
        }
        if ( q + 1 < insns.size() && insns[q].itype == wasmhex_local_get
             && uint32(insns[q].op1) == sp_local )
        {
          if ( is_load_op(insns[q + 1].itype) )
          {
            record(q + 1, 0);
            continue;
          }
          if ( q + 2 < insns.size() && is_value_push(insns[q + 1].itype)
               && is_store_op(insns[q + 2].itype) )
            record(q + 2, 0);
        }
      }
      for ( go_call_param_t &param : shape.params )
      {
        if ( !param.is_group || param.bytes != 16 || param.window_store_eas.size() != 2 )
          continue;
        const uint32 base = param.offset;
        const size_t s0 = insn_at(param.window_store_eas[0]);
        const size_t s1 = insn_at(param.window_store_eas[1]);
        const size_t call_idx = insn_at(state_entry + call.call_offset);
        if ( s0 == SIZE_MAX || s1 == SIZE_MAX || call_idx == SIZE_MAX )
          continue;
        const size_t min_s = s0 < s1 ? s0 : s1;
        // Reaching definition on the timeline: the two group stores write
        // the cells, the call reads them. Any OTHER window touch of these
        // cells must sit BEFORE the first group store or AFTER the call
        // (the outgoing window is shared with other calls — a neighbour
        // call's fills are fine outside the interval, a touch inside
        // (min_s, call_idx) changes or observes the value the call sees).
        bool cells_clean = true;
        for ( const window_access_t &access : accesses )
        {
          const bool touches
            = access.offset < base + 16 && base < access.offset + uint32(access.width);
          if ( !touches )
            continue;
          if ( access.at == s0 || access.at == s1 )
            continue; // the group's own two stores
          if ( access.at > min_s && access.at < call_idx )
          {
            cells_clean = false; // a foreign read/write between the fill and the call
            break;
          }
        }
        for ( const auto &escape : escaped )
        {
          const uint32 escaped_offset = escape.first;
          const bool touches = escaped_offset < base + 16 && base <= escaped_offset;
          if ( !touches )
            continue;
          if ( escape.second > min_s && escape.second < call_idx )
          {
            cells_clean = false; // the cell address escaped inside the interval
            break;
          }
        }
        if ( !cells_clean )
          continue;
        // Value shapes: slot0/slot1 constants, or locals fed by a unique
        // constant-address load (`i64.const A; wrap; i64.load off=0;
        // local.set L`, exactly one setter in the body).
        const auto slot_source
          = [&](size_t store_at, uint64 *constant, uint64 *load_address, uint32 *load_local) -> bool
        {
          if ( store_at == SIZE_MAX || store_at == 0 )
            return false;
          if ( insns[store_at - 1].itype == wasmhex_i64_const )
          {
            *constant = insns[store_at - 1].op1;
            return true;
          }
          if ( insns[store_at - 1].itype != wasmhex_local_get )
            return false;
          const uint32 source_local = uint32(insns[store_at - 1].op1);
          // Reaching definition on the local: the LAST `local.set L` before
          // the store (a local reused across calls reloads each time —
          // counting setters would reject the common case).
          size_t setter = SIZE_MAX;
          for ( size_t q = store_at; q != 0; --q )
            if ( insns[q - 1].itype == wasmhex_local_set
                 && uint32(insns[q - 1].op1) == source_local )
            {
              setter = q - 1;
              break;
            }
          if ( setter == SIZE_MAX || setter < 3 )
            return false;
          if ( insns[setter - 3].itype != wasmhex_i64_const
               || insns[setter - 2].itype != wasmhex_i32_wrap_i64
               || insns[setter - 1].itype != wasmhex_i64_load || insns[setter - 1].op1 != 0 )
            return false; // the load's memarg must be 0 — the constant is the address
          *load_address = insns[setter - 3].op1;
          *load_local = source_local;
          return true;
        };
        uint64 c0 = 0, c1 = 0, a0 = 0, a1 = 0;
        uint32 l0 = UINT32_MAX, l1 = UINT32_MAX;
        const bool src0 = slot_source(s0, &c0, &a0, &l0);
        const bool src1 = slot_source(s1, &c1, &a1, &l1);
        if ( !src0 || !src1 )
          continue;
        const bool both_const = l0 == UINT32_MAX && l1 == UINT32_MAX && s0 != SIZE_MAX
          && s1 != SIZE_MAX && insns[s0 - 1].itype == wasmhex_i64_const
          && insns[s1 - 1].itype == wasmhex_i64_const;
        const bool both_load = l0 != UINT32_MAX && l1 != UINT32_MAX && l0 != l1 && a1 == a0 + 8;
        if ( !both_const && !both_load )
          continue;
        param.fold_single_use = true;
        if ( both_load )
        {
          param.fold_global_source = true;
          param.fold_global_address = a0;
        }
        else
        {
          param.fold_const_pair = true;
        }
        for ( const ea_t store_ea : param.window_store_eas )
        {
          shape.fold_store_eas.push_back(store_ea);
          go_window_fold_store_eas.insert(store_ea);
        }
      }
    }
    for ( const ea_t group_store : group_window_stores )
      go_call_group_stores.insert(group_store);
    go_call_shapes[state_entry + call.call_offset] = std::move(shape);
  }
}

namespace
{
std::map<ea_t, std::vector<std::pair<ea_t, qstring>>> &go_pending_type_notes()
{
  static std::map<ea_t, std::vector<std::pair<ea_t, qstring>>> notes;
  return notes;
}

// Entry EA -> call-argument literals ({call EA, argument index}).
// DATA_EA is the linear-memory EA the string bytes were read from — the
// swapped cot_str anchors there (see make_go_string_literal).
struct go_arg_literal_t
{
  ea_t call_ea = BADADDR;
  size_t arg_index = 0;
  qstring text;
  ea_t data_ea = BADADDR;
};

std::map<ea_t, std::vector<go_arg_literal_t>> &go_pending_arg_literals()
{
  static std::map<ea_t, std::vector<go_arg_literal_t>> literals;
  return literals;
}

// C string args: the C-side twin of the pending
// literals above. emit_call (lower_calls.cpp) records an entry only after
// the full proof chain holds — the parameter position is pointer-typed
// (known-libc table / callee prototype / the loader's body-proof blob '4')
// and the constant argument resolves into the linear-memory segment. A
// validated NUL-terminated printable span swaps to a string literal; BSS
// or non-printable data swaps to `&item`. Fail-closed: nothing recorded
// means nothing swapped and the numeric display stays.
struct c_arg_swap_t
{
  ea_t call_ea = BADADDR;
  size_t arg_index = 0;
  bool literal = false;
  std::string text; // raw bytes; escaped at swap time (go_escape_c_literal)
  ea_t item_ea = BADADDR;
  ea_t data_ea = BADADDR; // literal form: the EA the string span was read at
};

std::map<ea_t, std::vector<c_arg_swap_t>> &c_pending_arg_swaps()
{
  static std::map<ea_t, std::vector<c_arg_swap_t>> swaps;
  return swaps;
}

// Defined below; needed here for the swap-time escaping.
qstring go_escape_c_literal(const std::string &text);

// Same one-pass discipline as go_arg_literal_swap_t: match by call EA +
// argument index, swap exactly once per recorded entry.
class c_arg_swap_visitor_t final : public ctree_visitor_t
{
public:
  const std::vector<c_arg_swap_t> &pending;
  std::set<const c_arg_swap_t *> swapped;
  explicit c_arg_swap_visitor_t(const std::vector<c_arg_swap_t> &p)
      : ctree_visitor_t(CV_FAST), pending(p)
  {
  }

  int idaapi visit_expr(cexpr_t *expression) override
  {
    if ( expression->op != cot_call || expression->a == nullptr )
      return 0;
    for ( const c_arg_swap_t &want : pending )
    {
      if ( want.call_ea != expression->ea || want.arg_index >= expression->a->size()
           || swapped.find(&want) != swapped.end() )
        continue;
      cexpr_t *argument = &(*expression->a)[int(want.arg_index)];
      // The literal anchors at its data EA — never at this argument's EA
      // (an instruction address): Hex-Rays persists ctree literals into
      // the IDB's synthetic-string registry at the literal EA, and a
      // call-site anchor pollutes the Strings window with .text entries.
      // A literal without its data anchor fails closed (numeric stays).
      cexpr_t *replacement = want.literal
        ? (want.data_ea == BADADDR
             ? nullptr
             : make_go_string_literal(go_escape_c_literal(want.text), want.data_ea))
        : make_data_address_literal(want.item_ea);
      if ( replacement != nullptr )
      {
        argument->replace_by(replacement);
        swapped.insert(&want);
      }
    }
    return 0;
  }
};

// Entry EA -> receiver member notes, keyed by the ACCESS instruction
// EA. Access EAs live inside expressions, so the callback first maps each
// onto its enclosing statement (user comments bind to statement EAs) —
// without that step Hex-Rays prints them as trailing orphan comments.
struct go_member_note_t
{
  ea_t access_ea = BADADDR;
  qstring text;
};

std::map<ea_t, std::vector<go_member_note_t>> &go_pending_member_notes()
{
  static std::map<ea_t, std::vector<go_member_note_t>> notes;
  return notes;
}

bool &go_type_notes_cb_installed()
{
  static bool installed = false;
  return installed;
}

// One pass over the final ctree; every recorded call site keeps the
// argument expression it was proven with (by EA + index — an engine-mangled
// or missing argument just leaves the numeric display, fail-closed).
class go_arg_literal_swap_t final : public ctree_visitor_t
{
public:
  const std::vector<go_arg_literal_t> &pending;
  std::set<const go_arg_literal_t *> swapped;
  explicit go_arg_literal_swap_t(const std::vector<go_arg_literal_t> &p)
      : ctree_visitor_t(CV_FAST), pending(p)
  {
  }

  int idaapi visit_expr(cexpr_t *expression) override
  {
    if ( expression->op != cot_call || expression->a == nullptr )
      return 0;
    for ( const go_arg_literal_t &want : pending )
    {
      if ( want.call_ea != expression->ea || want.arg_index >= expression->a->size()
           || want.text.empty() || want.data_ea == BADADDR || swapped.find(&want) != swapped.end() )
        continue;
      // Several nested expressions carry the call EA (the cast around the
      // callee etc.) — swap exactly once per pending literal.
      cexpr_t *argument = &(*expression->a)[int(want.arg_index)];
      // The literal anchors at the data EA the string bytes were proven
      // at, never at this argument's instruction EA (see
      // c_arg_swap_visitor_t for the registry/Strings-window mechanism).
      cexpr_t *literal = make_go_string_literal(want.text, want.data_ea);
      argument->replace_by(literal);
      swapped.insert(&want);
    }
    return 0;
  }
};

// Read `n` bytes of linear memory at a wasm address (EA ==
// wasm addr). Bytes the loader never patched inside the one MEMORY
// segment are zero-initialized linear memory (the wasm spec) — the BSS a
// static `any` data word points at (runtime.zerobase and friends) reads
// as zeroes even though get_bytes has nothing loaded there.
bool go_read_linear_memory(uint64 address, size_t n, uint8 *out)
{
  if ( out == nullptr )
    return false;
  const ea_t at = ea_t(address);
  const ssize_t got = get_bytes(out, n, at);
  if ( got == ssize_t(n) )
    return true;
  // getseg is deprecated (SDK >= 9.4); segment_info_t carries the
  // same start_ea/end_ea range without the pointer-lifetime hazard.
  segment_info_t seg;
  if ( !get_segment_info(&seg, at) || seg.start_ea != ea_t(wasmhex_memory_base) )
    return false; // outside the linear-memory segment: no honest bytes
  if ( address + n > uint64(seg.end_ea) )
    return false; // crosses the memory extent: would trap, not read zeroes
  if ( got < 0 )
    memset(out, 0, n);
  else
    memset(out + got, 0, n - size_t(got));
  return true;
}

// Map each member-note access EA onto its enclosing statement and
// merge the texts (two accesses in one statement share the comment).
class go_member_note_placer_t final : public ctree_visitor_t
{
public:
  cfunc_t *cfunc;
  const std::vector<go_member_note_t> &pending;
  std::set<const go_member_note_t *> placed;
  std::map<ea_t, qstring> by_statement;

  go_member_note_placer_t(cfunc_t *cf, const std::vector<go_member_note_t> &p)
      : ctree_visitor_t(CV_FAST), cfunc(cf), pending(p)
  {
  }

  int idaapi visit_expr(cexpr_t *expression) override
  {
    for ( const go_member_note_t &note : pending )
    {
      if ( note.access_ea != expression->ea || placed.find(&note) != placed.end() )
        continue;
      // Nested expressions (casts, the wrapping deref) share the access
      // instruction EA — place each note exactly once, at the FIRST match.
      placed.insert(&note);
      citem_t *item = expression;
      cinsn_t *statement = nullptr;
      while ( item != nullptr )
      {
        citem_t *parent = cfunc->body.find_parent_of(item);
        if ( parent == nullptr )
          break;
        if ( !parent->is_expr() ) // reached a statement (cinsn_t) node
        {
          statement = static_cast<cinsn_t *>(parent);
          break;
        }
        item = parent;
      }
      if ( statement == nullptr )
        continue;
      qstring &merged = by_statement[statement->ea];
      if ( !merged.empty() )
        merged += "; ";
      merged += note.text;
    }
    return 0;
  }
};

// Escape the validated UTF-8 text into a C literal body — Go
// strings freely contain newlines/tabs/quotes; the ctree printer emits
// cot_str bytes verbatim, so raw control characters or quotes would
// produce invalid C (a copy-pasted literal must compile).
qstring go_escape_c_literal(const std::string &text)
{
  qstring escaped;
  escaped.reserve(text.size());
  for ( char c : text )
  {
    switch ( c )
    {
      case '"': escaped += "\\\""; break;
      case '\\': escaped += "\\\\"; break;
      case '\n': escaped += "\\n"; break;
      case '\r': escaped += "\\r"; break;
      case '\t': escaped += "\\t"; break;
      default:
        if ( unsigned(char(c)) < 0x20 )
        {
          char buf[8];
          qsnprintf(buf, sizeof(buf), "\\x%02x", unsigned(char(c)) & 0xff);
          escaped += buf;
        }
        else
          escaped += c;
        break;
    }
  }
  return escaped;
}

ssize_t idaapi go_type_notes_hexrays_callback(void *, hexrays_event_t event, va_list va)
{
  if ( event != hxe_maturity )
    return 0;
  va_list va_copy_list;
  va_copy(va_copy_list, va);
  cfunc_t *cfunc = va_arg(va_copy_list, cfunc_t *);
  const int maturity = va_arg(va_copy_list, int);
  va_end(va_copy_list);
  if ( cfunc == nullptr || maturity != CMAT_FINAL )
    return 0;
  {
    auto &literals = go_pending_arg_literals();
    const auto lit = literals.find(cfunc->entry_ea);
    if ( lit != literals.end() )
    {
      if ( !lit->second.empty() )
      {
        go_arg_literal_swap_t swap(lit->second);
        swap.apply_to(&cfunc->body, nullptr);
      }
      literals.erase(lit);
    }
  }
  {
    // C string args: swap the proven constant pointer arguments (recorded
    // during emit_call) for literals / &item — display only, same channel
    // as the Go literals above.
    auto &swaps = c_pending_arg_swaps();
    const auto found = swaps.find(cfunc->entry_ea);
    if ( found != swaps.end() )
    {
      if ( !found->second.empty() )
      {
        c_arg_swap_visitor_t visitor(found->second);
        visitor.apply_to(&cfunc->body, nullptr);
      }
      swaps.erase(found);
    }
  }
  {
    auto &members = go_pending_member_notes();
    const auto mem = members.find(cfunc->entry_ea);
    if ( mem != members.end() )
    {
      if ( !mem->second.empty() )
      {
        go_member_note_placer_t placer(cfunc, mem->second);
        placer.apply_to(&cfunc->body, nullptr);
        for ( const auto &placed : placer.by_statement )
        {
          treeloc_t loc;
          loc.ea = placed.first;
          loc.itp = ITP_SEMI;
          cfunc->set_user_cmt(loc, placed.second.c_str());
        }
      }
      members.erase(mem);
    }
  }
  auto &pending = go_pending_type_notes();
  const auto found = pending.find(cfunc->entry_ea);
  if ( found == pending.end() )
    return 0;
  for ( const auto &note : found->second )
  {
    if ( note.first == BADADDR || note.second.empty() )
      continue;
    treeloc_t loc;
    loc.ea = note.first;
    loc.itp = ITP_SEMI;
    cfunc->set_user_cmt(loc, note.second.c_str());
  }
  pending.erase(found);
  return 0;
}

void ensure_go_type_notes_cb_installed()
{
  if ( !go_type_notes_cb_installed() && get_hexdsp() != nullptr )
  {
    go_type_notes_cb_installed()
      = install_hexrays_callback(go_type_notes_hexrays_callback, nullptr);
  }
}

void remove_go_type_notes_cb()
{
  if ( go_type_notes_cb_installed() && get_hexdsp() != nullptr )
  {
    remove_hexrays_callback(go_type_notes_hexrays_callback, nullptr);
    go_type_notes_cb_installed() = false;
  }
  go_pending_type_notes().clear();
  go_pending_arg_literals().clear();
  go_pending_member_notes().clear();
  c_pending_arg_swaps().clear();
}

// Build marker, verified via strings on the deployed procs dylib.
extern "C" __attribute__((used)) const char kWasmhexG7S4TypeNotesMarker[]
  = "WASMHEX_BUILD_MARKER_G7_S4_TYPE_NOTES_AND_NAMED_GROUPS";
// Build marker (string literals at shaped call arguments, box()
// / args notes, receiver member notes — same verification pattern).
extern "C" __attribute__((used)) const char kWasmhexM3GoValueDisplayMarker[]
  = "WASMHEX_BUILD_MARKER_M3_GO_VALUE_DISPLAY";
// C string args build marker (constant pointer-typed call arguments render
// as &item / string literals — proof extension).
extern "C" __attribute__((used)) const char kWasmhexCStringArgsMarker[]
  = "WASMHEX_BUILD_MARKER_C_STRING_ARGS_SWAP";
// String-literal anchor marker: every swapped ctree literal (Go, C
// string args, rewriter __memory_base) anchors at its linear-memory data
// EA — never at a call-site/instruction EA — so the IDB synthetic-string
// registry (Strings window) stays free of .text "Decompiler" rows.
extern "C" __attribute__((used)) const char kWasmhexLiteralAnchorMarker[]
  = "WASMHEX_BUILD_MARKER_STRING_LITERAL_DATA_ANCHOR";
} // namespace

void record_c_arg_string_literal(ea_t entry_ea, ea_t call_ea, size_t arg_index,
                                 const std::string &text, ea_t data_ea)
{
  if ( entry_ea == BADADDR || call_ea == BADADDR || text.empty() || data_ea == BADADDR )
    return;
  ensure_go_type_notes_cb_installed(); // C modules never run the Go collectors
  c_arg_swap_t swap;
  swap.call_ea = call_ea;
  swap.arg_index = arg_index;
  swap.literal = true;
  swap.text = text;
  swap.data_ea = data_ea;
  c_pending_arg_swaps()[entry_ea].push_back(std::move(swap));
}

void record_c_arg_data_address(ea_t entry_ea, ea_t call_ea, size_t arg_index, ea_t item_ea)
{
  if ( entry_ea == BADADDR || call_ea == BADADDR || item_ea == BADADDR )
    return;
  ensure_go_type_notes_cb_installed(); // C modules never run the Go collectors
  c_arg_swap_t swap;
  swap.call_ea = call_ea;
  swap.arg_index = arg_index;
  swap.literal = false;
  swap.item_ea = item_ea;
  c_pending_arg_swaps()[entry_ea].push_back(std::move(swap));
}

void clear_c_arg_records(ea_t entry_ea)
{
  c_pending_arg_swaps().erase(entry_ea);
}

void wasmhex_filter_t::collect_go_type_notes(netnode &wasm_node,
                                             const loader::go_module_index_t &index,
                                             const loader::go_function_ir_t &go_ir)
{
  if ( !go_ir.present || state_entry == BADADDR || wasm_node == BADNODE )
    return;
  // The decoded layer comes from the ModuleIndex (decode once
  // per IDB). The absent/malformed gates collapse into layer_present +
  // the same present/descriptors checks the per-F5 decode applied.
  if ( !index.layer_present )
    return;
  const loader::go_type_info_t &layer = index.layer;
  if ( !layer.present || layer.descriptors.empty() )
    return; // malformed/absent layer: no descriptor anchor, no note
  // The owner probe stays as the function-existence gate (the body
  // view below comes from the decode-once stream).
  func_entry_info_t owner;
  if ( !get_func_entry_info(&owner, state_entry) )
    return;
  // One linear decode of the body.
  struct note_insn_t
  {
    ea_t ea;
    int itype;
    uint64 op1;
  };
  qvector<note_insn_t> insns;
  // Local view over the decode-once stream (see shapes).
  if ( active_function_ir == nullptr || !active_function_ir->stream_valid )
    return;
  insns.reserve(active_function_ir->insns.size());
  for ( const loader::go_insn_record_t &record : active_function_ir->insns )
    insns.push_back(note_insn_t{ ea_t(record.ea), int(record.itype), record.op1 });
  const ea_t self_start = get_func_start(state_entry); // ea-based, BADADDR when absent
  ensure_go_type_notes_cb_installed();                 // idempotent; runs once per process

  // ---- map call sites: `runtime.makemap*(desc, ...)` and the map
  // assign/access/delete family whose FIRST window argument is a constant
  // equal to a map-kind descriptor address (the Go compiler bakes the
  // *maptype into every statically typed map operation).
  for ( const loader::go_call_ir_t &call : go_ir.calls )
  {
    if ( call.is_pc_f_fast_return || call.fn_index == UINT32_MAX )
      continue;
    const ea_t callee_ea = wasm_node.eaget_idx(call.fn_index, 'F');
    if ( callee_ea == BADADDR || callee_ea == 0 )
      continue;
    qstring callee_qname;
    if ( get_func_name(&callee_qname, callee_ea) <= 0 )
      continue;
    const std::string callee_name = callee_qname.c_str();
    const char *op = nullptr;
    if ( callee_name == "runtime.makemap" || callee_name == "runtime.makemap64" )
      op = "make";
    else if ( callee_name.rfind("runtime.mapassign", 0) == 0 )
      op = "mapassign";
    else if ( callee_name.rfind("runtime.mapaccess", 0) == 0 )
      op = "mapaccess";
    else if ( callee_name.rfind("runtime.mapdelete", 0) == 0 )
      op = "mapdelete";
    if ( op == nullptr )
      continue;
    // The first outgoing-window store (arg sp_offset 0) must be pushed by a
    // bare i64.const right before the store.
    uint32 store_offset = UINT32_MAX;
    for ( const loader::go_arg_ir_t &arg : call.args )
      if ( arg.sp_offset == 0 && arg.store_offset != UINT32_MAX )
      {
        store_offset = arg.store_offset;
        break;
      }
    if ( store_offset == UINT32_MAX )
      continue;
    const ea_t store_ea = state_entry + store_offset;
    size_t store_index = insns.size();
    for ( size_t k = insns.size(); k != 0; --k )
      if ( insns[k - 1].ea == store_ea )
      {
        store_index = k - 1;
        break;
      }
    if ( store_index == 0 || store_index == insns.size()
         || insns[store_index].itype != wasmhex_i64_store
         || insns[store_index - 1].itype != wasmhex_i64_const )
      continue;
    const uint64 descriptor_addr = insns[store_index - 1].op1;
    const loader::go_type_descriptor_t *descriptor = index.find_descriptor(descriptor_addr);
    if ( descriptor == nullptr || descriptor->kind != 21 ) // abi.Map
      continue;
    const std::string map_type = loader::go_display_type_name(layer, descriptor_addr);
    if ( map_type.empty() )
      continue; // gap-cut key/elem: no honest name, no note
    qstring note;
    note.sprnt("go: %s %s", op, map_type.c_str());
    go_pending_type_notes()[state_entry].push_back(
      { state_entry + call.call_offset, std::move(note) });
  }

  const std::unordered_map<uint32, uint32> &slot_to_fn = index.table0_slot_to_fn;
  const auto load_table0_slots = [&]() -> bool
  {
    return !slot_to_fn.empty();
  };
  for ( size_t k = 1; k < insns.size(); ++k )
  {
    if ( insns[k].itype != wasmhex_i64_store || insns[k - 1].itype != wasmhex_i64_const )
      continue;
    const uint64 value = insns[k - 1].op1;
    if ( value == 0 )
      continue;
    uint64 slot = 0;
    bool slot_ok = false;
    if ( (value & 0xffffu) == 0 )
    {
      slot = value >> 16; // direct PC_F constant (go1.27)
      slot_ok = true;
    }
    else if ( value < 0x100000000ULL )
    {
      // Static rodata funcval pointer (go1.26): read its code word from the
      // instantiated MEMORY segment.
      uint8 bytes[8] = {};
      if ( get_bytes(bytes, sizeof(bytes), ea_t(wasmhex_memory_base + value)) == sizeof(bytes) )
      {
        const uint64 funcval = uint64(bytes[0]) | uint64(bytes[1]) << 8 | uint64(bytes[2]) << 16
          | uint64(bytes[3]) << 24 | uint64(bytes[4]) << 32 | uint64(bytes[5]) << 40
          | uint64(bytes[6]) << 48 | uint64(bytes[7]) << 56;
        if ( funcval != 0 && (funcval & 0xffffu) == 0 )
        {
          slot = funcval >> 16;
          slot_ok = true;
        }
      }
    }
    if ( !slot_ok || slot == 0 || slot > UINT32_MAX )
      continue;
    if ( !load_table0_slots() )
      continue;
    const auto found = slot_to_fn.find(uint32(slot));
    if ( found == slot_to_fn.end() )
      continue;
    const ea_t target_ea = wasm_node.eaget_idx(found->second, 'F');
    if ( target_ea == BADADDR || target_ea == 0 || target_ea == self_start )
      continue; // unmapped, or this function's own morestack push
    // get_func_cmt_ea by address — the get_func/get_func_cmt pair is
    // deprecated (SDK >= 9.4); -1 (no function) keeps the gate.
    qstring anchor_cmt;
    if ( get_func_cmt_ea(&anchor_cmt, target_ea, true) <= 0 || anchor_cmt.length() < 4
         || strncmp(anchor_cmt.c_str(), "go: ", 4) != 0 )
      continue; // no anchor: the target's signature is unproven
    qstring target_name;
    if ( get_func_name(&target_name, target_ea) <= 0 || target_name.empty() )
      continue;
    qstring note;
    note.sprnt("go: closure %s %s", target_name.c_str(), anchor_cmt.c_str() + 4);
    go_pending_type_notes()[state_entry].push_back({ insns[k].ea, std::move(note) });
  }
}

void wasmhex_filter_t::collect_go_value_evidence(netnode &wasm_node,
                                                 const loader::go_module_index_t &index,
                                                 const loader::go_function_ir_t &go_ir)
{
  go_pending_arg_literals()[state_entry].clear();
  go_pending_member_notes()[state_entry].clear();
  if ( !go_ir.present || state_entry == BADADDR || wasm_node == BADNODE )
    return;
  if ( go_call_shapes.empty() && !go_frame_mode )
    return; // nothing provable: need shapes, a frame
  // Decoded layer via the ModuleIndex (see collect_go_type_notes).
  if ( !index.layer_present )
    return;
  const loader::go_type_info_t &layer = index.layer;
  if ( !layer.present || layer.descriptors.empty() )
    return;
  func_entry_info_t owner;
  if ( !get_func_entry_info(&owner, state_entry) )
    return;
  // One linear decode of the body.
  struct value_insn_t
  {
    ea_t ea;
    int itype;
    uint64 op1;
  };
  qvector<value_insn_t> insns;
  // Local view over the decode-once stream (see shapes).
  if ( active_function_ir == nullptr || !active_function_ir->stream_valid )
    return;
  insns.reserve(active_function_ir->insns.size());
  for ( const loader::go_insn_record_t &record : active_function_ir->insns )
    insns.push_back(value_insn_t{ ea_t(record.ea), int(record.itype), record.op1 });
  const auto insn_index = [&](ea_t ea) -> size_t
  {
    for ( size_t k = insns.size(); k != 0; --k )
      if ( insns[k - 1].ea == ea )
        return k - 1;
    return SIZE_MAX;
  };
  const auto is_quintet_at = [&](size_t j) -> bool
  {
    // insns[j .. j+4] == local.get SP; i64.extend_i32_u; i64.const K;
    // i64.add; i32.wrap_i64 (the standard computed window address).
    return j + 4 < insns.size() && insns[j].itype == wasmhex_local_get
      && insns[j + 1].itype == wasmhex_i64_extend_i32_u && insns[j + 2].itype == wasmhex_i64_const
      && insns[j + 3].itype == wasmhex_i64_add && insns[j + 4].itype == wasmhex_i32_wrap_i64;
  };
  // Address constant of the i64.store at index m, when the address is one
  // of the Go codegen forms (returns false when the shape is unrecognized —
  // every consumer then fails closed). Forms observed on go1.27 bodies:
  //   A [quintet K][value][store 0]      — memarg 0, K inside the quintet
  //   B [local.get SP][value][store M]   — bare base, offset in the memarg
  //   C [local.get SP; local.get SP; i64.extend_i32_u; i64.const k;
  //      i64.add][store M]               — SP+k VALUE, offset in the memarg
  // Forms B/C must sit on the frame anchor local (the proven SP cache) —
  // any other base is not a frame cell and must not look like one.
  const auto store_address_const = [&](size_t m, uint64 *address) -> bool
  {
    const uint64 memarg = insns[m].op1;
    const bool frame_anchor_ready
      = go_frame_mode && size_t(c_frame_metadata.fp_local_index) < locals.size();
    if ( m >= 6 && is_quintet_at(m - 6) && memarg == 0 )
    {
      *address = insns[m - 4].op1;
      return true;
    }
    if ( m >= 5 && is_quintet_at(m - 5) && memarg == 0 )
    {
      *address = insns[m - 3].op1;
      return true;
    }
    if ( frame_anchor_ready && m >= 2 && insns[m - 2].itype == wasmhex_local_get
         && uint32(insns[m - 2].op1) == c_frame_metadata.fp_local_index )
    {
      *address = memarg;
      return true; // form B
    }
    if ( frame_anchor_ready && m >= 5 && insns[m - 5].itype == wasmhex_local_get
         && uint32(insns[m - 5].op1) == c_frame_metadata.fp_local_index
         && insns[m - 4].itype == wasmhex_local_get
         && uint32(insns[m - 4].op1) == c_frame_metadata.fp_local_index
         && insns[m - 3].itype == wasmhex_i64_extend_i32_u
         && insns[m - 2].itype == wasmhex_i64_const && insns[m - 1].itype == wasmhex_i64_add )
    {
      *address = memarg;
      return true; // form C
    }
    return false;
  };
  size_t count_literals = 0;
  size_t count_boxing = 0;
  size_t count_variadic = 0;
  size_t count_members = 0;

  // Constant (ptr, len) string pairs on shaped call arguments.
  for ( auto &shape_entry : go_call_shapes )
  {
    for ( size_t param_index = 0; param_index < shape_entry.second.params.size(); ++param_index )
    {
      const go_call_param_t &param = shape_entry.second.params[param_index];
      if ( !param.is_group || param.bytes != 16 )
        continue;
      // Named groups (go__iface, named structs, go_string) keep their
      // proven identity; only go_string and the UNNAMED extent form are
      // candidates for a literal — a named non-string 16-byte group
      // proves the pair is NOT a string.
      if ( param.group_type != "go_string" && param.group_type != "go_agg16" )
        continue;
      if ( param.window_store_eas.size() != 2 )
        continue;
      const size_t ptr_store = insn_index(param.window_store_eas[0]);
      const size_t len_store = insn_index(param.window_store_eas[1]);
      if ( ptr_store == SIZE_MAX || len_store == SIZE_MAX || ptr_store == 0 || len_store == 0 )
        continue;
      if ( insns[ptr_store - 1].itype != wasmhex_i64_const
           || insns[len_store - 1].itype != wasmhex_i64_const )
        continue; // dynamic pair: the real stores stay (fail-closed)
      const uint64 string_ptr = insns[ptr_store - 1].op1;
      const uint64 string_len = insns[len_store - 1].op1;
      constexpr uint64 kMaxGoLiteralBytes = 1ull << 20;
      if ( string_len == 0 || string_len > kMaxGoLiteralBytes )
        continue;
      if ( index.find_descriptor(string_ptr) != nullptr )
        continue; // an interface {type, data} word, not a string pointer
      std::vector<std::uint8_t> bytes;
      bytes.resize(size_t(string_len));
      if ( get_bytes(bytes.data(), bytes.size(), ea_t(string_ptr)) != bytes.size() )
        continue; // unmapped/truncated span: keep the numeric display
      std::string text;
      if ( !loader::go_string_literal_text(bytes.data(), bytes.size(), &text) )
        continue;
      go_arg_literal_t literal;
      literal.call_ea = shape_entry.first;
      literal.arg_index = param_index;
      literal.text = go_escape_c_literal(text);
      literal.data_ea = ea_t(string_ptr); // anchor: the real data EA
      go_pending_arg_literals()[state_entry].push_back(std::move(literal));
      ++count_literals;
    }
  }

  // Inline interface boxing pairs + shaped variadic backing arrays.
  if ( go_frame_mode )
  {
    // Leading descriptor arguments of runtime metadata calls are type
    // METADATA (makemap* bake the *maptype into the call), never boxed
    // values — their first window store is excluded from the pair scan.
    const auto runtime_metadata_callee = [](const std::string &name) -> bool
    {
      static const char *families[]
        = { "runtime.makemap",     "runtime.mapassign",      "runtime.mapaccess",
            "runtime.mapdelete",   "runtime.mapiter",        "runtime.convT",
            "runtime.newobject",   "runtime.newarray",       "runtime.makeslice",
            "runtime.growslice",   "runtime.typedmemmove",   "runtime.typedslicecopy",
            "runtime.typedmemclr", "runtime.typehash",       "runtime.panicdottype",
            "runtime.typeAssert",  "runtime.interfaceSwitch" };
      for ( const char *family : families )
        if ( name.rfind(family, 0) == 0 )
          return true;
      return false;
    };
    std::set<ea_t> metadata_arg_stores;
    for ( const loader::go_call_ir_t &call : go_ir.calls )
    {
      if ( call.is_pc_f_fast_return || call.fn_index == UINT32_MAX )
        continue;
      const ea_t callee_ea = wasm_node.eaget_idx(call.fn_index, 'F');
      if ( callee_ea == BADADDR || callee_ea == 0 )
        continue;
      qstring callee_qname;
      if ( get_func_name(&callee_qname, callee_ea) <= 0
           || !runtime_metadata_callee(callee_qname.c_str()) )
        continue;
      for ( const loader::go_arg_ir_t &arg : call.args )
        if ( arg.sp_offset == 0 && arg.store_offset != UINT32_MAX )
        {
          metadata_arg_stores.insert(state_entry + arg.store_offset);
          break;
        }
    }
    constexpr uint8 kBoxExcludedKindMap = 21;
    constexpr uint8 kBoxExcludedKindFunc = 19;
    constexpr uint8 kBoxExcludedKindIface = 20;
    constexpr uint8 kBoxKindString = 24;
    // Index the frame stores once: the FIRST i64.store at every resolvable
    // constant frame offset (cells are written before their call), and the
    // subset whose value is a descriptor-const type word (box candidates).
    std::map<uint64, size_t> any_store_by_addr;
    std::map<uint64, size_t> box_type_store_by_addr;
    for ( size_t k = 1; k < insns.size(); ++k )
    {
      if ( insns[k].itype != wasmhex_i64_store )
        continue;
      uint64 address = 0;
      if ( !store_address_const(k, &address) )
        continue;
      any_store_by_addr.emplace(address, k);
      if ( insns[k - 1].itype != wasmhex_i64_const )
        continue;
      const loader::go_type_descriptor_t *descriptor = index.find_descriptor(insns[k - 1].op1);
      if ( descriptor == nullptr || descriptor->kind == kBoxExcludedKindMap
           || descriptor->kind == kBoxExcludedKindFunc
           || descriptor->kind == kBoxExcludedKindIface )
        continue;
      if ( metadata_arg_stores.find(insns[k].ea) != metadata_arg_stores.end() )
        continue;
      box_type_store_by_addr.emplace(address, k);
    }
    // The display text of one proven box: "go: box(kind)" / "go: box(kind)
    // = <static value>" for notes, or the bare "kind [= value]" form the
    // variadic args list reuses. Dynamic data (the common &stack-cell case)
    // keeps the kind alone — the visible data store right below names the
    // value.
    const auto boxed_text = [&](const loader::go_type_descriptor_t &descriptor, size_t data_store,
                                bool bare = false) -> qstring
    {
      const std::string kind_name = loader::go_descriptor_type_name(descriptor);
      if ( kind_name.empty() )
        return "";
      qstring suffix; // "" or " = <static value>"
      if ( data_store != SIZE_MAX && insns[data_store - 1].itype == wasmhex_i64_const )
      {
        const uint64 data_address = insns[data_store - 1].op1;
        qstring value;
        if ( descriptor.kind == kBoxKindString )
        {
          // The data word points at a {ptr, len} header — read the literal.
          uint8 header[16] = {};
          if ( data_address != 0 && go_read_linear_memory(data_address, sizeof(header), header) )
          {
            const auto read_u64 = [&](size_t at) -> uint64
            {
              uint64 v = 0;
              for ( unsigned b = 0; b < 8; ++b )
                v |= uint64(header[at + b]) << (b * 8);
              return v;
            };
            const uint64 ptr = read_u64(0);
            const uint64 len = read_u64(8);
            std::vector<std::uint8_t> bytes;
            if ( len > 0 && len < (1ull << 20) && index.find_descriptor(ptr) == nullptr )
            {
              bytes.resize(size_t(len));
              if ( go_read_linear_memory(ptr, bytes.size(), bytes.data()) )
              {
                std::string text;
                if ( loader::go_string_literal_text(bytes.data(), bytes.size(), &text) )
                  value.sprnt("\"%s\"", go_escape_c_literal(text).c_str());
              }
            }
          }
        }
        else
        {
          uint8 data8[8] = {};
          if ( data_address != 0 && go_read_linear_memory(data_address, sizeof(data8), data8) )
          {
            std::string scalar;
            if ( loader::go_boxed_scalar_text(descriptor.kind, data8, &scalar) )
              value = scalar.c_str();
          }
        }
        if ( !value.empty() )
          suffix.sprnt(" = %s", value.c_str());
      }
      if ( bare )
        return qstring().sprnt("%s%s", kind_name.c_str(), suffix.c_str());
      return qstring().sprnt("go: box(%s)%s", kind_name.c_str(), suffix.c_str());
    };
    // Per-pair notes for every proven inline box (window cells AND variadic
    // backing arrays — the shape is identical and position-independent).
    for ( const auto &box : box_type_store_by_addr )
    {
      const size_t data_store = [&]() -> size_t
      {
        const auto data = any_store_by_addr.find(box.first + 8);
        return data == any_store_by_addr.end() ? SIZE_MAX : data->second;
      }();
      const loader::go_type_descriptor_t *descriptor
        = index.find_descriptor(insns[box.second - 1].op1);
      const qstring note = boxed_text(*descriptor, data_store);
      if ( note.empty() )
        continue;
      go_pending_type_notes()[state_entry].push_back({ insns[box.second].ea, note });
      ++count_boxing;
    }
    // Variadic `// go: args = {...}`: the shaped call's trailing go_slice
    // with constant len and an SP+K0 pointer whose whole backing array is
    // tiled by proven pairs (all-or-nothing, fail-closed).
    for ( const auto &shape_entry : go_call_shapes )
    {
      if ( shape_entry.second.params.empty() )
        continue;
      const go_call_param_t &last = shape_entry.second.params.back();
      if ( !last.is_group || last.bytes != 24 || last.window_store_eas.size() != 3 )
        continue;
      if ( last.group_type != "go_slice" && last.group_type != "go_agg24" )
        continue;
      const size_t ptr_store = insn_index(last.window_store_eas[0]);
      const size_t len_store = insn_index(last.window_store_eas[1]);
      if ( ptr_store == SIZE_MAX || len_store == SIZE_MAX || ptr_store < 4 || len_store == 0 )
        continue;
      // ptr value: [local.get SP; i64.extend_i32_u; i64.const K0; i64.add]
      if ( insns[ptr_store - 4].itype != wasmhex_local_get
           || insns[ptr_store - 3].itype != wasmhex_i64_extend_i32_u
           || insns[ptr_store - 2].itype != wasmhex_i64_const
           || insns[ptr_store - 1].itype != wasmhex_i64_add )
        continue;
      const uint64 base_offset = insns[ptr_store - 2].op1;
      if ( insns[len_store - 1].itype != wasmhex_i64_const )
        continue;
      const uint64 count = insns[len_store - 1].op1;
      if ( count == 0 || count > 64 || base_offset + count * 16 > uint64(go_ir.frame_size) )
        continue;
      qstring args = "go: args = {";
      bool all_proven = true;
      for ( uint64 element = 0; element < count && all_proven; ++element )
      {
        const uint64 type_at = base_offset + element * 16;
        const auto type_store = box_type_store_by_addr.find(type_at);
        if ( type_store == box_type_store_by_addr.end() )
        {
          all_proven = false;
          break;
        }
        const loader::go_type_descriptor_t *descriptor
          = index.find_descriptor(insns[type_store->second - 1].op1);
        const auto data_store_entry = any_store_by_addr.find(type_at + 8);
        const size_t data_store
          = data_store_entry == any_store_by_addr.end() ? SIZE_MAX : data_store_entry->second;
        // Bare form: "kind" or "kind = <static value>" (the note wrapper
        // would nest badly inside the args list).
        const qstring entry = boxed_text(*descriptor, data_store, true);
        if ( entry.empty() )
        {
          all_proven = false;
          break;
        }
        if ( element != 0 )
          args += ", ";
        args += entry;
      }
      if ( !all_proven )
        continue;
      args += "}";
      go_pending_type_notes()[state_entry].push_back({ shape_entry.first, args });
      ++count_variadic;
    }
  }

  // Pointer-receiver member accesses on the proven Go frame.
  if ( go_frame_mode && size_t(c_frame_metadata.fp_local_index) < locals.size()
       && go_ir.frame_valid(uint32(owner.size())) )
  {
    // Receiver struct from the function's own name (pkg.__T_.m is the
    // pointer-receiver wrapper spelling), same resolution as the
    // callee-receiver naming in collect_go_call_shapes.
    qstring self_qname;
    if ( get_func_name(&self_qname, state_entry) > 0 )
    {
      const std::string self_name = self_qname.c_str();
      const size_t first_dot = self_name.find('.');
      const size_t last_dot = self_name.rfind('.');
      if ( first_dot != std::string::npos && last_dot != first_dot
           && self_name.find('.', first_dot + 1) == last_dot )
      {
        const std::string pkg = self_name.substr(0, first_dot);
        const std::string type_name = self_name.substr(first_dot + 1, last_dot - first_dot - 1);
        const size_t pkg_tail_pos = pkg.rfind('_');
        const std::string pkg_tail
          = pkg_tail_pos == std::string::npos ? pkg : pkg.substr(pkg_tail_pos + 1);
        // The IDA name now carries the canonical display form
        // `pkg.(*T).m` (set_cp_validity opened '(' ')' '*' at processor
        // init); the wasm-mangled `pkg.__T_.m` stays accepted for old
        // IDBs. Both spellings yield the same bare type via substr(2,
        // size-3) — `__T_` drops `__`/`_`, `(*T)` drops `(*`/`)`.
        const bool pointer_form_mangled
          = type_name.size() > 3 && type_name.compare(0, 2, "__") == 0 && type_name.back() == '_';
        const bool pointer_form_display = type_name.size() >= 4 && type_name[0] == '('
          && type_name[1] == '*' && type_name.back() == ')';
        const bool pointer_form = pointer_form_mangled || pointer_form_display;
        if ( pointer_form )
        {
          const std::string want = type_name.substr(2, type_name.size() - 3);
          const bool plain_type = !want.empty()
            && want.find_first_not_of("abcdefghijklmnopqrstuvwxyz"
                                      "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
              == std::string::npos;
          const loader::go_type_descriptor_t *receiver = nullptr;
          int matches = 0;
          if ( plain_type && !pkg.empty() )
          {
            const std::string candidates[2] = { pkg + "." + want, pkg_tail + "." + want };
            for ( const loader::go_type_descriptor_t &descriptor : layer.descriptors )
            {
              if ( descriptor.kind != 25 )
                continue;
              const std::string display = loader::go_descriptor_type_name(descriptor);
              if ( display != candidates[0] && display != candidates[1] )
                continue;
              ++matches;
              receiver = &descriptor;
            }
          }
          if ( matches == 1 && receiver != nullptr )
          {
            const uint32 frame_local = c_frame_metadata.fp_local_index;
            const uint64 receiver_word = uint64(go_ir.frame_size) + 8;
            const auto access_width = [](int itype) -> int
            {
              if ( itype == wasmhex_i64_load || itype == wasmhex_f64_load
                   || itype == wasmhex_i64_store || itype == wasmhex_f64_store )
                return 8;
              if ( itype == wasmhex_i32_load || itype == wasmhex_f32_load
                   || itype == wasmhex_i32_store || itype == wasmhex_f32_store )
                return 4;
              return 0;
            };
            for ( size_t a = 3; a < insns.size(); ++a )
            {
              const int width = access_width(insns[a].itype);
              if ( width == 0 )
                continue;
              // Shape: [local.get SP; i64.load W; i32.wrap_i64] access, or
              // the same with the field offset folded in front:
              // [local.get SP; i64.load W; i64.const K; i64.add;
              //  i32.wrap_i64] access.
              uint64 field_offset = 0;
              bool shape_ok = false;
              if ( a >= 3 && insns[a - 3].itype == wasmhex_local_get
                   && uint32(insns[a - 3].op1) == frame_local
                   && insns[a - 2].itype == wasmhex_i64_load && insns[a - 2].op1 == receiver_word
                   && insns[a - 1].itype == wasmhex_i32_wrap_i64 )
              {
                field_offset = insns[a].op1; // memarg
                shape_ok = true;
              }
              else if ( a >= 5 && insns[a - 5].itype == wasmhex_local_get
                        && uint32(insns[a - 5].op1) == frame_local
                        && insns[a - 4].itype == wasmhex_i64_load
                        && insns[a - 4].op1 == receiver_word
                        && insns[a - 3].itype == wasmhex_i64_const
                        && insns[a - 2].itype == wasmhex_i64_add
                        && insns[a - 1].itype == wasmhex_i32_wrap_i64 )
              {
                field_offset = insns[a - 3].op1 + insns[a].op1; // K + memarg
                shape_ok = true;
              }
              if ( !shape_ok )
                continue;
              std::string field;
              if ( !loader::go_member_covering(layer, *receiver, field_offset, uint64(width),
                                               &field) )
                continue; // padding / straddling: no honest name
              // Proven pointer-receiver member access — the wrap EA feeds
              // the member lowering route (the 8-byte
              // receiver value rides to the member load unchanged, see
              // emit_integer_conversion / emit_fixed_memory).
              go_receiver_wrap_eas.insert(insns[a - 1].ea);
              // Loads render the REAL member through that route
              // (`a1->color.len`) — the annotation would now duplicate the
              // render. Only when the registered til struct actually has a
              // member covering the byte range (a descriptor-proven name
              // whose til field walk did not close would otherwise LOSE its
              // only honest label: the render falls back to `a1 + K`).
              if ( insns[a].itype == wasmhex_i64_load || insns[a].itype == wasmhex_i32_load
                   || insns[a].itype == wasmhex_f64_load || insns[a].itype == wasmhex_f32_load )
              {
                bool til_covers = false;
                tinfo_t receiver_udt;
                if ( receiver_udt.get_named_type(
                       get_idati(), loader::go_descriptor_c_name(*receiver).c_str(), BTF_STRUCT)
                     && receiver_udt.is_udt() )
                {
                  udt_type_data_t udt;
                  if ( receiver_udt.get_udt_details(&udt) )
                  {
                    const uint64 base = field_offset;
                    const uint64 end = field_offset + uint64(width);
                    for ( const udm_t &member : udt )
                    {
                      const uint64 m_base = uint64(member.offset / 8);
                      const uint64 m_end = m_base + uint64(member.size);
                      if ( m_base <= base && end <= m_end )
                      {
                        til_covers = true;
                        break;
                      }
                    }
                  }
                }
                if ( til_covers )
                  continue; // the member renders for real; no note needed
              }
              go_member_note_t note;
              note.access_ea = insns[a].ea;
              // go-sig-drop-pcb: the receiver parameter is a2 while the raw
              // PC_B selector keeps `int a1@<r0>`, a1 after the loader drops
              // it (pc_b_register_proof) — mirror the applied prototype so
              // the note names the parameter the reader sees.
              note.text.sprnt("go: %s->%s", go_ir.pc_b_register_proof ? "a1" : "a2", field.c_str());
              go_pending_member_notes()[state_entry].push_back(std::move(note));
              ++count_members;
            }
          }
        }
      }
    }
  }
  if ( options::debug("goval") )
    msg("wasmhex: go value display: strings=%zu boxing=%zu variadic=%zu members=%zu at %a\n",
        count_literals, count_boxing, count_variadic, count_members, state_entry);
}

bool wasmhex_filter_t::collect_go_jmps(const loader::go_function_ir_t &go_ir)
{
  go_jmps.clear();
  go_jmp_by_const.clear();
  go_jmp_by_set.clear();
  go_jmp_by_br.clear();
  go_dispatch_ea = BADADDR;
  go_dispatch_selector_ea = BADADDR;
  go_dispatch_targets.clear();
  go_dispatch_depths.clear();
  go_direct_edges = 0;
  const bool trace = options::debug("g2");
  func_entry_info_t owner;
  if ( !get_func_entry_info(&owner, state_entry) || !go_ir.present )
    return false;

  if ( !go_ir.pc_b_register_proof || go_ir.resume_table.dispatch_offset == UINT32_MAX
       || go_ir.resume_table.dispatch_offset < 2 )
  {
    if ( trace )
      msg("G2T entry=%a DISABLED (no dispatcher or PC_B register proof failed)\n", state_entry);
    return false;
  }

  const ea_t dispatch_ea = state_entry + go_ir.resume_table.dispatch_offset;
  const ea_t selector_ea = dispatch_ea - 2;
  // after the selector must sit exactly at the dispatcher (the old single
  // decode's `selector_ea + sel_insn.size == dispatch_ea`).
  if ( active_function_ir == nullptr || !active_function_ir->stream_valid )
    return false;
  const std::size_t sel_index = active_function_ir->find(selector_ea);
  if ( sel_index == SIZE_MAX || active_function_ir->insns[sel_index].itype != wasmhex_local_get
       || uint32(active_function_ir->insns[sel_index].op1) != 0
       || sel_index + 1 >= active_function_ir->insns.size()
       || active_function_ir->insns[sel_index + 1].ea != dispatch_ea )
    return false;

  qvector<ea_t> dispatch_targets;
  qvector<uint32> dispatch_depths;
  dispatch_targets.reserve(go_ir.resume_table.targets.size());
  dispatch_depths.reserve(go_ir.resume_table.depths.size());
  for ( uint32 target_off : go_ir.resume_table.targets )
    dispatch_targets.push_back(state_entry + target_off);
  for ( uint32 depth : go_ir.resume_table.depths )
    dispatch_depths.push_back(depth);

  bool dispatcher_ok
    = dispatch_targets.size() >= 2 && dispatch_targets.size() == dispatch_depths.size();
  for ( size_t k = 0; k < dispatch_targets.size() && dispatcher_ok; ++k )
  {
    if ( dispatch_targets[k] <= state_entry
         || dispatch_targets[k] >= state_entry + uint64(owner.size()) )
      dispatcher_ok = false;
  }
  if ( dispatcher_ok && dispatch_targets[0] <= dispatch_ea )
  {
    // Synthetic Go shells (s2/s7/s11/p05) place `loop $entry` inside
    // `block $resume` so arm 0 is the loop back-edge and the main body
    // starts at the first forward block target (`block $resume` end) before
    // the outer `$unwind` exit. Require at least two distinct forward
    // targets (`resume` and `unwind`) when arm 0 is backward.
    ea_t min_fwd = BADADDR, max_fwd = 0;
    for ( ea_t t : dispatch_targets )
      if ( t > dispatch_ea )
      {
        if ( t < min_fwd )
          min_fwd = t;
        if ( t > max_fwd )
          max_fwd = t;
      }
    if ( min_fwd == BADADDR || min_fwd == max_fwd )
      dispatcher_ok = false;
  }
  if ( trace )
    msg("G2T entry=%a first_br_table=%a table=%zu selector_local0=1 "
        "metadata=%s -> %s\n",
        state_entry, dispatch_ea, dispatch_targets.size(),
        dispatch_targets.empty() ? "MISSING" : "ok",
        dispatcher_ok ? "dispatcher accepted" : "DISABLED (not the entryPointLoop shape)");
  if ( !dispatcher_ok )
    return false;

  std::vector<go_jmp_t> records;
  records.reserve(go_ir.jmps.size());
  for ( const loader::go_jmp_ir_t &jmp : go_ir.jmps )
  {
    go_jmp_t record;
    record.const_ea = state_entry + jmp.const_offset;
    record.set_ea = state_entry + jmp.set_offset;
    record.br_ea = state_entry + jmp.br_offset;
    record.case_index = jmp.case_index;
    records.push_back(record);
  }

  for ( const go_jmp_t &record : records )
  {
    // The last table entry is the default. Compare without uint32 overflow:
    // case_index may be 0xFFFFFFFF from a valid i32.const -1.
    if ( size_t(record.case_index) >= dispatch_targets.size() - 1
         || dispatch_targets[record.case_index] <= state_entry
         || dispatch_targets[record.case_index] >= state_entry + uint64(owner.size()) )
    {
      if ( trace )
        msg("G2T entry=%a DISABLED: k=%u out of range (table=%zu)\n", state_entry,
            record.case_index, dispatch_targets.size());
      return false;
    }
  }
  go_dispatch_ea = dispatch_ea;
  go_dispatch_selector_ea = selector_ea;
  go_dispatch_targets = dispatch_targets;
  go_dispatch_depths = dispatch_depths;
  go_jmps = std::move(records);
  for ( size_t i = 0; i < go_jmps.size(); ++i )
  {
    go_jmp_by_const[go_jmps[i].const_ea] = i;
    go_jmp_by_set[go_jmps[i].set_ea] = i;
    go_jmp_by_br[go_jmps[i].br_ea] = i;
  }
  if ( trace )
    msg("G2T entry=%a ENABLED: records=%zu table=%zu dispatcher=%a\n", state_entry, go_jmps.size(),
        go_dispatch_targets.size(), go_dispatch_ea);
  return true;
}

bool wasmhex_filter_t::reset_state(codegen_t &cdg)
{
  // A function carrying an 'r' (rejected-control) record was
  // withheld by the loader because its control metadata failed the
  // fail-closed range gate. Decompiling it would consume absent metadata
  // as "unknown control" and invent a CFG — refuse with the reason.
  {
    netnode reject_node(wasmhex_node_name, 0, false);
    if ( reject_node != BADNODE )
    {
      struct rejected_record_t
      {
        uchar version;
        uint32 bad_controls;
        ea_t first_control;
      } record;
      if ( reject_node.supval_ea(cdg.mba->entry_ea, &record, sizeof(record), 'r') == sizeof(record)
           && record.version == 1 )
      {
        msg("WASMHEX_F5_UNSUPPORTED: function %a rejected by the loader "
            "(%u control(s) failed the range gate, first at %a) — "
            "re-load the module with the current loader\n",
            cdg.mba->entry_ea, record.bad_controls, record.first_control);
        return false;
      }
      // The emulator's v2 cfg-reject record — a control edge was
      // refused while building this function's CFG. Refuse F5 with the
      // recorded reason and BOTH EAs; no pseudocode is produced for a
      // half-known graph. (v2 overwrote any earlier v1 mark; both refuse.)
      // Wire: 0x02 | reason | ea from | ea to (little endian; matches
      // store_cfg_reject — no padding, unlike a raw struct read).
      uchar cfg_reject[2 + sizeof(ea_t) * 2];
      if ( reject_node.supval_ea(cdg.mba->entry_ea, cfg_reject, sizeof(cfg_reject), 'u')
             == sizeof(cfg_reject)
           && cfg_reject[0] == 2 )
      {
        ea_t edge[2] = {};
        for ( unsigned side = 0; side < 2; ++side )
          for ( unsigned byte = 0; byte < sizeof(ea_t); ++byte )
            edge[side] |= ea_t(cfg_reject[2 + side * sizeof(ea_t) + byte]) << (byte * 8);
        msg("WASMHEX_F5_UNSUPPORTED: function %a refused: the control edge "
            "%a -> %a was rejected while building the CFG (%s) — "
            "decompilation would invent control flow\n",
            cdg.mba->entry_ea, edge[0], edge[1],
            cfg_reject_reason(wasmhex_cfg_reject_t(cfg_reject[1])));
        return false;
      }
      // The validator stopped early on this function (unmodeled
      // opcode family) — its code was never checked, so F5 refuses.
      uchar unsupported_mark = 0;
      if ( reject_node.supval_ea(cdg.mba->entry_ea, &unsupported_mark, sizeof(unsupported_mark),
                                 'u')
             == sizeof(unsupported_mark)
           && unsupported_mark == 1 )
      {
        msg("WASMHEX_F5_UNSUPPORTED: function %a uses constructs the "
            "validator does not model (unsupported state) — decompilation "
            "refused\n",
            cdg.mba->entry_ea);
        return false;
      }
    }
  }
  if ( state_ready && state_mba == cdg.mba && state_entry == cdg.mba->entry_ea )
    return true;
  // Measure this collection batch (skips the per-instruction fast path
  // above, so one line == one real collection).
  const std::chrono::steady_clock::time_point irtime_t0 = std::chrono::steady_clock::now();
  const double irtime_blob_before = irtime::blob_ms_total();
  const double irtime_body_before = irtime::body_ms_total();
  const unsigned irtime_blob_builds_before = go_module_index_builds();
  const unsigned irtime_body_builds_before = go_function_ir_builds();
  struct irtime_emit_t
  {
    ea_t entry;
    std::chrono::steady_clock::time_point t0;
    double blob_before, body_before;
    unsigned blob_builds_before, body_builds_before;
    ~irtime_emit_t()
    {
      if ( !irtime::on() )
        return;
      const double total_ms
        = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
      // Under idalib msg() does not reach any sink during decompilation —
      // write straight to fd 2 like the "mc" trace (the sweep pipes this).
      char buf[192];
      const int n = qsnprintf(
        buf, sizeof(buf),
        "IRTIME entry=%a total=%.3f blob=%.3f body=%.3f blob_builds=%u body_builds=%u\n", entry,
        total_ms, irtime::blob_ms_total() - blob_before, irtime::body_ms_total() - body_before,
        go_module_index_builds() - blob_builds_before,
        go_function_ir_builds() - body_builds_before);
      if ( n > 0 )
        ::write(2, buf, size_t(n));
    }
  } irtime_emit{ cdg.mba->entry_ea,         irtime_t0,
                 irtime_blob_before,        irtime_body_before,
                 irtime_blob_builds_before, irtime_body_builds_before };
  clear_runtime_state();
  state_mba = cdg.mba;
  state_entry = cdg.mba->entry_ea;
  // C string args: stale records for this entry (a decompile that never
  // reached CMAT_FINAL) must not leak into the next run.
  clear_c_arg_records(state_entry);
  netnode wasm_node(wasmhex_node_name, 0, false);
  // Load the call-protocol records for this function (blob 'x'):
  // per-site store / SP-refresh / br_if offsets (in-body) that apply()
  // consumes so the presentation keeps only the plain call + resume.
  go_call_store_eas.clear();
  go_call_sp_refresh_eas.clear();
  go_call_br_eas.clear();
  go_call_noreturn_br_eas.clear();
  go_call_sp_set_locals.clear();
  go_call_arg_store_types.clear();
  go_call_const_eas.clear();
  go_call_args_by_call.clear();
  go_call_sp_arith_eas.clear();
  go_pcf_extract_eas.clear();
  go_pcf_call_fn_by_ea.clear();
  go_pcf_loop_call_ea = BADADDR;
  go_call_shapes.clear();
  go_empty_args_store_eas.clear();
  go_window_fold_store_eas.clear();
  go_call_group_stores.clear();
  loader::go_function_ir_t loaded_go_ir;
  const loader::go_module_index_t *module_index = nullptr;
  if ( wasm_node != BADNODE )
  {
    // valid — a malformed blob leaves the function exactly as if 'x' were
    // absent (fail-closed), never a partial "plumbing-only" consumption
    // of the records that happened to parse.
    bool go_records_ok = false;
    qvector<ea_t> tmp_store_eas;
    qvector<ea_t> tmp_refresh_eas;
    qvector<ea_t> tmp_br_eas;
    qvector<ea_t> tmp_noreturn_br_eas;
    qvector<ea_t> tmp_const_eas;
    std::map<ea_t, uchar> tmp_arg_store_types;
    std::map<ea_t, qvector<uchar>> tmp_args_by_call;
    std::set<ea_t> tmp_sp_arith_eas;
    std::map<ea_t, uint32> tmp_sp_set_locals;
    std::set<ea_t> tmp_pcf_extract_eas;
    std::map<ea_t, uint32> tmp_pcf_call_fn_by_ea;
    ea_t tmp_pcf_loop_call_ea = BADADDR;
    // ONE fetch serves the blob-'x' decode, the ModuleIndex and
    // the body stream (validity = module identity + body bytes + raw blob
    // bytes, proven inside; nullptr = no 'x' blob, the non-Go path pays
    // nothing). The body scope covers the whole accessor: identity hashes
    // per F5, the linear stream decode only on a miss.
    {
      irtime::scope_t irtime_body(irtime::scope_t::kind_t::body);
      active_function_ir = go_function_ir(wasm_node, state_entry);
    }
    if ( active_function_ir != nullptr && active_function_ir->wire_valid )
    {
      bool ok = true;
      loaded_go_ir = active_function_ir->ir;
      // Fetch the ModuleIndex for THIS database (decode-once
      // identity-checked — the irtime scope covers the accessor: one
      // getblob+hash per F5, a full blob-'t' decode only on the identity
      // miss). All Go collectors below read this one index. Reachable
      // whenever a blob 'x' decoded (NOT loaded_go_ir.present: a legacy v2
      // stream decodes with present=false yet carries valid call records
      // the collectors consume).
      {
        irtime::scope_t irtime_blob(irtime::scope_t::kind_t::blob);
        module_index = go_module_index(wasm_node);
      }
      for ( size_t r = 0; r < loaded_go_ir.calls.size() && ok; ++r )
      {
        const loader::go_call_ir_t &call = loaded_go_ir.calls[r];
        const ea_t call_ea = state_entry + call.call_offset;
        if ( !call.is_pc_f_fast_return )
        {
          tmp_store_eas.push_back(state_entry + call.store_offset);
          tmp_br_eas.push_back(state_entry + call.br_offset);
          if ( call.is_noreturn_call )
            tmp_noreturn_br_eas.push_back(state_entry + call.br_offset);
        }
        tmp_refresh_eas.push_back(state_entry + call.sp_restore_offset);
        qvector<uchar> &arg_types = tmp_args_by_call[call_ea];
        for ( size_t a = 0; a < call.args.size() && ok; ++a )
        {
          const uint32 opcode = call.args[a].opcode;
          const uint32 store_offset = call.args[a].store_offset;
          // Opcode bytes come from the shared single-source table
          // 0x2C is i32.load8_s, f32.store is 0x38.
          const uchar value_type = opcode == k_go_i64_store ? uchar(0x7E)
            : opcode == k_go_i32_store                      ? uchar(0x7F)
            : opcode == k_go_f32_store                      ? uchar(0x7D)
            : opcode == k_go_f64_store                      ? uchar(0x7C)
                                                            : uchar(0);
          if ( value_type == 0 )
          {
            ok = false;
            break;
          }
          tmp_arg_store_types[state_entry + store_offset] = value_type;
          arg_types.push_back(value_type);
        }
        // The `41 00` PC_B push precedes EVERY recorded Go call —
        // zero-argument calls too.
        if ( ok )
        {
          const ea_t const_ea = call.pc_b_const_offset != UINT32_MAX
            ? state_entry + call.pc_b_const_offset
            : call_ea - 2;
          tmp_const_eas.push_back(const_ea);
        }
      }

      if ( ok )
      {
        if ( loaded_go_ir.present && loaded_go_ir.sp_refresh.size() == loaded_go_ir.calls.size() )
        {
          for ( size_t r = 0; r < loaded_go_ir.calls.size() && ok; ++r )
          {
            const loader::go_call_ir_t &call = loaded_go_ir.calls[r];
            const loader::go_sp_refresh_ir_t &ref = loaded_go_ir.sp_refresh[r];
            if ( call.sp_cache_local == UINT32_MAX || ref.local_index != call.sp_cache_local
                 || ref.get_offset != call.sp_restore_offset )
            {
              ok = false;
              break;
            }
            if ( !call.is_pc_f_fast_return )
            {
              for ( int k = 0; k < 5; ++k )
              {
                if ( call.sp_arith_offsets[k] == UINT32_MAX )
                {
                  ok = false;
                  break;
                }
                tmp_sp_arith_eas.insert(state_entry + call.sp_arith_offsets[k]);
              }
            }
            if ( !ok )
              break;
            if ( call.is_pc_f_indirect )
            {
              func_entry_info_t owner;
              if ( !get_func_entry_info(&owner, state_entry)
                   || !call.pc_f_protocol_valid(uint32(owner.size())) )
              {
                ok = false;
                break;
              }
              for ( int k = 0; k < 5; ++k )
                tmp_pcf_extract_eas.insert(state_entry + call.pc_f_extract_offsets[k]);
              tmp_pcf_call_fn_by_ea[state_entry + call.call_offset] = call.fn_index;
            }
            // The pair is verified against the cached decode-once
            // stream: the refresh EA must be an instruction boundary whose
            // record is `global.get 0`, and the NEXT record must be the
            // `local.set L` of this protocol instance (its EA is exactly
            // refresh_ea + insn.size of the old single decode).
            const ea_t refresh_ea = state_entry + ref.get_offset;
            const loader::function_ir_t *fir = active_function_ir;
            const std::size_t get_index
              = fir != nullptr && fir->stream_valid ? fir->find(refresh_ea) : SIZE_MAX;
            if ( get_index == SIZE_MAX || fir->insns[get_index].itype != wasmhex_global_get
                 || uint32(fir->insns[get_index].op1) != 0 || get_index + 1 >= fir->insns.size() )
            {
              ok = false;
              break;
            }
            const ea_t set_ea = ea_t(fir->insns[get_index + 1].ea);
            if ( set_ea != state_entry + ref.set_offset
                 || fir->insns[get_index + 1].itype != wasmhex_local_set
                 || uint32(fir->insns[get_index + 1].op1) != ref.local_index )
            {
              ok = false;
              break;
            }
            tmp_sp_set_locals[set_ea] = ref.local_index;
          }
          if ( ok && loaded_go_ir.is_pc_f_loop )
          {
            func_entry_info_t owner;
            if ( !get_func_entry_info(&owner, state_entry)
                 || !loaded_go_ir.pc_f_loop_valid(uint32(owner.size())) )
            {
              ok = false;
            }
            else
            {
              // Single-instruction verify (Op2 = table index — not in the
              // stream records; one decode for the rare pc_f_loop body).
              const ea_t loop_call_ea = state_entry + loaded_go_ir.pc_f_loop_call_offset;
              insn_t loop_call_insn;
              if ( decode_insn(&loop_call_insn, loop_call_ea) <= 0
                   || loop_call_insn.itype != wasmhex_call_indirect
                   || uint32(loop_call_insn.Op2.value) != 0 )
                ok = false;
              else
                tmp_pcf_loop_call_ea = loop_call_ea;
            }
          }
        }
        else
        {
          // Legacy v2 blob (e.g. go1.19-legacy): fall back to the stream
          // collectors for SP-arith and refresh sets.
          std::map<ea_t, uint32> tmp_sp_cache_local_by_store;
          std::map<ea_t, std::pair<ea_t, uint32>> tmp_refresh_info;
          ok = collect_go_call_sp_arith(tmp_store_eas, &tmp_sp_arith_eas,
                                        &tmp_sp_cache_local_by_store)
            && collect_go_call_sp_refresh_sets(tmp_refresh_eas, &tmp_refresh_info);
          if ( ok )
          {
            for ( size_t r = 0; r < loaded_go_ir.calls.size(); ++r )
            {
              const ea_t store_ea = state_entry + loaded_go_ir.calls[r].store_offset;
              const ea_t refresh_ea = state_entry + loaded_go_ir.calls[r].sp_restore_offset;
              const auto cache_local = tmp_sp_cache_local_by_store.find(store_ea);
              const auto refresh = tmp_refresh_info.find(refresh_ea);
              if ( cache_local == tmp_sp_cache_local_by_store.end()
                   || refresh == tmp_refresh_info.end()
                   || refresh->second.second != cache_local->second )
              {
                ok = false;
                break;
              }
              tmp_sp_set_locals[refresh->second.first] = refresh->second.second;
            }
          }
        }
      }
      go_records_ok = ok;
    }
    if ( go_records_ok )
    {
      // These EA lists are probed for EVERY decoded instruction
      // keep them sorted so the probes are binary searches instead of the
      // linear std::find scans they used to be.
      auto sort_ea_vector = [](qvector<ea_t> &v)
      {
        std::sort(v.begin(), v.end());
      };
      sort_ea_vector(tmp_store_eas);
      sort_ea_vector(tmp_refresh_eas);
      sort_ea_vector(tmp_br_eas);
      sort_ea_vector(tmp_noreturn_br_eas);
      sort_ea_vector(tmp_const_eas);
      go_call_store_eas = std::move(tmp_store_eas);
      go_call_sp_refresh_eas = std::move(tmp_refresh_eas);
      go_call_br_eas = std::move(tmp_br_eas);
      go_call_noreturn_br_eas = std::move(tmp_noreturn_br_eas);
      if ( !go_call_noreturn_br_eas.empty() )
        ensure_go_structural_flattener_installed();
      go_call_const_eas = std::move(tmp_const_eas);
      go_call_arg_store_types = std::move(tmp_arg_store_types);
      go_call_args_by_call = std::move(tmp_args_by_call);
      go_call_sp_arith_eas = std::move(tmp_sp_arith_eas);
      go_call_sp_set_locals = std::move(tmp_sp_set_locals);
      go_pcf_extract_eas = std::move(tmp_pcf_extract_eas);
      go_pcf_call_fn_by_ea = std::move(tmp_pcf_call_fn_by_ea);
      go_pcf_loop_call_ea = tmp_pcf_loop_call_ea;
      collect_go_call_shapes(wasm_node, *module_index, loaded_go_ir);
      collect_go_type_notes(wasm_node, *module_index, loaded_go_ir);
      for ( const ea_t group_store : go_call_group_stores )
        go_call_arg_store_types.erase(group_store);
    }
    else if ( active_function_ir != nullptr )
    {
      // A present but undecodable blob 'x' (wire_valid false — the
      // fail-closed refusal of the old load_go_function_ir path).
      loaded_go_ir = loader::go_function_ir_t{};
      msg("WASMHEX_F5_UNSUPPORTED: malformed Go call-protocol record at %a "
          "(blob 'x' rejected; plumbing renders as plain code)\n",
          state_entry);
    }
  }
  // Populate the entryPointLoop const-JMP records from the pre-built Go
  // function IR (fail-closed clears the transform on any mismatch).
  collect_go_jmps(loaded_go_ir);
  if ( cdg.mba->fullsize < 0 || state_entry == BADADDR )
    return false;
  declared_local_types.clear();
  tinfo_t declared_type;
  if ( wasm_node == BADNODE || !get_wasm_bounds_mode(wasm_node, &bounds_mode) )
    return false;
  if ( !get_type_vector(wasm_node, state_entry, '4', &function_params) )
    return false;
  if ( !get_type_vector(wasm_node, state_entry, '5', &function_results) )
    return false;
  go_xf_wire_t xf_wire = {};
  const bool go_xf_active = loaded_go_ir.results_caller_proven() && function_results.size() > 1
    && wasm_node.supval_ea(state_entry, &xf_wire, sizeof(xf_wire), 'U') == sizeof(xf_wire)
    && xf_wire.version == kGoXfWireVersion && xf_wire.slot_count == function_results.size();
  const bool go_display_multi_sret = !go_xf_active && loaded_go_ir.results_caller_proven()
    && !loaded_go_ir.ret.empty() && function_results.size() > 1;
  if ( go_display_multi_sret
         ? !make_wasm_go_sret_function_type(function_params, function_results, &declared_type)
         : !make_wasm_function_type(function_params, function_results, false, &declared_type) )
    return false;
  if ( go_xf_active )
  {
    // Swap the tuple rettype for the loader-registered named struct (same
    // layout — the loader cross-checked sizes before writing 'U'). The
    // explicit locations must survive create_func (FTI_EXPLOCS) or the
    // engine ignores the scattered retloc.
    func_type_data_t xf_details;
    tinfo_t named;
    if ( !declared_type.get_func_details(&xf_details, GTD_CALC_ARGLOCS)
         || !xf_details.rettype.is_udt()
         || !named.get_named_type(get_idati(), xf_wire.c_name, BTF_STRUCT) || !named.is_udt()
         || named.get_size() != xf_details.rettype.get_size() )
      return false; // stale til or a tampered record: fail closed
    xf_details.rettype = named;
    xf_details.set_cc(CM_CC_SPECIAL);
    xf_details.flags |= FTI_ARGLOCS | FTI_EXPLOCS;
    if ( !declared_type.create_func(xf_details) )
      return false;
  }
  if ( !declared_type.get_func_details(&function_type, GTD_CALC_ARGLOCS) )
    return false;
  const bool tuple_sret = function_results.size() > 1
    && (go_display_multi_sret || wasm_results_size(function_results) > 16);
  if ( tuple_sret )
  {
    if ( function_type.size() != function_params.size() + 1 || !function_type.rettype.is_void()
         || !function_type[function_params.size()].type.is_ptr() )
      return false;
    has_sret_pointer = true;
  }
  if ( function_type.size() != function_params.size() + size_t(tuple_sret) )
    return false;
  for ( size_t i = 0; i < function_params.size(); ++i )
    if ( wasm_type_size(function_params[i]) != function_type[i].type.get_size() )
      return false;
  if ( (tuple_sret && !function_type.rettype.is_void())
       || (!tuple_sret && function_results.empty() != function_type.rettype.is_void()) )
    return false;
  if ( function_results.size() == 1
       && wasm_type_size(function_results[0]) != function_type.rettype.get_size() )
    return false;
  if ( function_results.size() > 1 )
  {
    size_t words = 0;
    if ( !multi_result_word_count(function_results, &words, has_sret_pointer)
         || (!tuple_sret && !function_type.rettype.is_udt())
         || (tuple_sret && !function_type.rettype.is_void()) )
      return false;
  }

  go_result_slots.clear();
  go_results_synthesis = false;
  if ( loaded_go_ir.results_caller_proven() && !loaded_go_ir.ret.empty() )
  {
    func_entry_info_t owner;
    std::vector<loader::go_result_slot_t> slots;
    std::vector<unsigned char> slot_types;
    if ( get_func_entry_info(&owner, state_entry) && loaded_go_ir.frame_valid(uint32(owner.size()))
         && loader::go_result_slots_sorted(loaded_go_ir, &slots)
         && loader::go_result_slot_types(loaded_go_ir, &slot_types)
         && slot_types.size() == size_t(function_results.size()) )
    {
      bool types_match = true;
      for ( size_t i = 0; i < slot_types.size(); ++i )
        if ( slot_types[i] != function_results[i] )
        {
          types_match = false;
          break;
        }
      if ( types_match )
      {
        go_result_slots = std::move(slots);
        go_results_synthesis = true;
      }
    }
  }

  bytevec_t encoded_locals;
  if ( wasm_node.getblob_ea(&encoded_locals, state_entry, 'L') > 0 )
  {
    declared_local_types.resize(encoded_locals.size());
    memcpy(declared_local_types.begin(), encoded_locals.begin(), encoded_locals.size());
  }
  else
  {
    const ssize_t nbytes = wasm_node.supval_ea(state_entry, nullptr, 0, 'L');
    if ( nbytes > 0 )
    {
      declared_local_types.resize(size_t(nbytes));
      if ( wasm_node.supval_ea(state_entry, declared_local_types.begin(),
                               declared_local_types.size(), 'L')
           != nbytes )
        return false;
    }
  }
  qvector<int64> declared_local_heap_types;
  qvector<uchar> declared_local_ref_forms;
  if ( !get_reference_metadata(wasm_node, state_entry, 'l', declared_local_types.size(),
                               &declared_local_heap_types, &declared_local_ref_forms) )
    return false;

  const size_t total_locals = function_params.size() + declared_local_types.size();
  const ssize_t c_frame_size
    = wasm_node.supval_ea(state_entry, &c_frame_metadata, sizeof(c_frame_metadata), 's');
  if ( c_frame_size >= 0 )
  {
    constexpr uint32 max_c_frame_bytes = 1024 * 1024;
    uchar sp_type = 0, sp_mutability = 0;
    ea_t sp_address = BADADDR;
    bool sp_imported = false;
    qstring sp_helper;
    if ( c_frame_size != ssize_t(sizeof(c_frame_metadata)) || c_frame_metadata.c_frame_bytes == 0
         || (c_frame_metadata.c_frame_bytes & 15u) != 0
         || c_frame_metadata.c_frame_bytes > max_c_frame_bytes
         || (c_frame_metadata.direction != -1 && c_frame_metadata.direction != 1)
         || size_t(c_frame_metadata.fp_local_index) < function_params.size()
         || size_t(c_frame_metadata.fp_local_index) >= total_locals
         || declared_local_types[size_t(c_frame_metadata.fp_local_index) - function_params.size()]
           != 0x7F
         || !get_global_metadata(c_frame_metadata.sp_global_index, &sp_type, &sp_mutability,
                                 &sp_address, &sp_imported, &sp_helper)
         || sp_type != 0x7F || sp_mutability == 0
         || sval_t(c_frame_metadata.c_frame_bytes) > cdg.mba->frsize )
    {
      msg("WASMHEX_F5_UNSUPPORTED: invalid C shadow stack frame metadata at %a\n", state_entry);
      return false;
    }
    has_c_frame = true;
  }
  if ( !has_c_frame )
  {
  }

  locals.reserve(total_locals);
  size_t next_reg = 0;
  for ( size_t i = 0; i < function_params.size(); ++i )
  {
    wasm_local_t &local = locals.push_back();
    local.tif = function_type[i].type;
    local.type = function_params[i];
    const int size = int(local.tif.get_size());
    mop_t argument;
    if ( wasm_type_size(local.type) != size || next_reg + size / 4 > wasmhex_user_regs
         || !make_regloc_mop(cdg, function_type[i].argloc, size, &argument) )
      return false;
    if ( !make_local_storage(cdg, local.type, &local.storage)
         || !emit_move(cdg, argument, local.storage, local.tif.is_floating()) )
      return false;
    local.initialized = true;
  }
  if ( tuple_sret )
  {
    const funcarg_t &result_arg = function_type[function_params.size()];
    mop_t argument;
    const mreg_t result_reg = cdg.mba->alloc_kreg(4);
    // The parsed `void *` param type follows the DB pointer size
    // (8 on wide databases); the wasm32 value still lives in the declared
    // 4-byte register slot, so materialize at the parsed width and narrow.
    const int sret_ptr_size = int(result_arg.type.get_size());
    if ( !result_arg.type.is_ptr() || (sret_ptr_size != 4 && sret_ptr_size != 8)
         || result_reg == mr_none
         || !make_regloc_mop(cdg, result_arg.argloc, sret_ptr_size, &argument) )
      return false;
    if ( sret_ptr_size == 8 && !argument.make_low_half(4) )
      return false;
    sret_pointer_reg = result_reg;
    const mop_t sret_pointer(result_reg, 4);
    if ( !emit_move(cdg, argument, sret_pointer, false) )
      return false;
    ++next_reg;
  }
  for ( size_t i = 0; i < declared_local_types.size(); ++i )
  {
    const size_t local_index = locals.size();
    wasm_local_t &local = locals.push_back();
    local.type = declared_local_types[i];
    if ( !get_wasm_tinfo(local.type, &local.tif) )
      return false;
    const int size = int(local.tif.get_size());
    if ( size != 4 && size != 8 )
      return false;
    if ( !make_local_storage(cdg, local.type, &local.storage) )
      return false;
    if ( has_c_frame && local_index == size_t(c_frame_metadata.fp_local_index) )
    {
      local.initialized = true;
      continue;
    }
    mop_t zero;
    if ( local.tif.is_floating() )
    {
      uint64 bits = 0;
      if ( !zero.make_fpnum(&bits, size_t(size)) )
        return false;
    }
    else
    {
      zero.make_number(0, size, state_entry, 0);
    }
    if ( !emit_move(cdg, zero, local.storage, local.tif.is_floating()) )
      return false;
    local.initialized = true;
  }
  go_frame_mode = false;
  if ( !has_c_frame && loaded_go_ir.present )
  {
    func_entry_info_t go_frame_owner;
    if ( get_func_entry_info(&go_frame_owner, state_entry)
         && loaded_go_ir.frame_valid(uint32(go_frame_owner.size()))
         && loaded_go_ir.frame_size <= 0xFFF0 // mirror the loader cap
         && (c_frame_size < 0 || c_frame_metadata.c_frame_bytes == 0)
         && collect_go_frame_legs(loaded_go_ir) )
    {
      uchar sp_type = 0, sp_mutability = 0;
      ea_t sp_address = BADADDR;
      bool sp_imported = false;
      qstring sp_helper;
      if ( get_global_metadata(0, &sp_type, &sp_mutability, &sp_address, &sp_imported, &sp_helper)
           && sp_type == 0x7F && sp_mutability != 0
           && size_t(c_frame_metadata.fp_local_index) < locals.size()
           && locals[size_t(c_frame_metadata.fp_local_index)].type == 0x7F
           && sval_t(loaded_go_ir.frame_size) <= cdg.mba->frsize )
      {
        c_frame_metadata.sp_global_index = 0;
        c_frame_metadata.c_frame_bytes = loaded_go_ir.frame_size;
        c_frame_metadata.direction = -1;
        has_c_frame = true;
        go_frame_mode = true;
        // The prologue leg's local.tee already "wrote" the anchor — every
        // later local.get L must yield the frame-address marker.
        c_frame_fp_written = true;
      }
    }
  }
  // Value display over proven evidence (string literals at
  // shaped call arguments, boxing/variadic notes, receiver member notes).
  // Runs after the go-frame decision: the pair/member scans key off the
  // frame anchor local and the proven frame size. Pure stashing — the
  // presentation happens at ctree maturity.
  if ( module_index != nullptr )
    collect_go_value_evidence(wasm_node, *module_index, loaded_go_ir);
  if ( get_func_start(state_entry) == BADADDR )
    return false;
  wasm_control_frame_t function_frame;
  function_frame.kind = 0x00;
  function_frame.start = state_entry;
  function_frame.base_stack = stack;
  function_frame.result_types = function_results;
  function_frame.label_types = function_results;
  for ( size_t i = 0; i < function_results.size(); ++i )
  {
    const uchar type = function_results[i];
    const int size = wasm_type_size(type);
    if ( size == 0 )
      continue;
    wasm_value_t &value = function_frame.result_values.push_back();
    value.type = type;
    if ( !make_operand_register(cdg, type, stack.size() + i, &value.op) )
      return false;
  }
  if ( function_frame.result_values.size() == function_results.size() )
    function_frame.label_values = function_frame.result_values;
  controls.push_back(function_frame);
  state_ready = true;
  return true;
}

void wasmhex_filter_t::push_c_sp_base()
{
  wasm_value_t &value = stack.push_back();
  value.type = 0x7F;
  value.is_c_sp_base = true;
}

void wasmhex_filter_t::push(const mop_t &op, uchar type, uint32 function_ref_index, bool owned_temp)
{
  // A value on the abstract operand stack must never live in
  // a scratch register - operand registers are keyed by (depth, type) and
  // scratch registers are keyed by (size, index); mixing them lets later
  // same-depth materializations overwrite live arguments.
  if ( op.t == mop_r && !scratch_register_index.empty() )
    for ( const auto &scratch : scratch_register_index )
      if ( scratch.second == op.r )
      {
        msg("WASMHEX_INVARIANT: scratch register r%d pushed onto the "
            "operand stack at depth %zu\n",
            int(op.r), stack.size());
        break;
      }
  wasm_value_t &value = stack.push_back();
  value.op = op;
  value.type = type;
  value.function_ref_index = function_ref_index;
  value.owned_temp = owned_temp;
}

bool wasmhex_filter_t::pop(uchar type, wasm_value_t *out, bool allow_c_frame_marker)
{
  if ( stack.empty() || stack.back().type != type )
    return false;
  if ( !allow_c_frame_marker && is_c_frame_marker(stack.back()) )
  {
    msg("WASMHEX_F5_UNSUPPORTED: C shadow stack marker used as a general scalar "
        "operand at %a\n",
        current_insn_ea);
    return false;
  }
  *out = stack.back();
  stack.pop_back();
  return true;
}

bool wasmhex_filter_t::stack_has_prefix(const qvector<wasm_value_t> &prefix) const
{
  if ( stack.size() < prefix.size() )
    return false;
  for ( size_t i = 0; i < prefix.size(); ++i )
    if ( stack[i].type != prefix[i].type )
      return false;
  return true;
}

bool wasmhex_filter_t::starts_with(const char *str, const char *prefix)
{
  return strncmp(str, prefix, strlen(prefix)) == 0;
}

void wasmhex_filter_t::clear_for_mba(mba_t *mba)
{
  if ( state_mba == mba )
    clear_runtime_state();
}

void wasmhex_filter_t::clear_for_shutdown()
{
  remove_go_structural_flattener();
  remove_go_type_notes_cb();
  clear_runtime_state();
}

bool wasmhex_filter_t::match(codegen_t &cdg)
{
  return PH.id == wasmhex_proc_id;
}

merror_t wasmhex_filter_t::apply_instruction(codegen_t &cdg)
{
  if ( !reset_state(cdg) )
  {
    msg("WASMHEX_F5_UNSUPPORTED: cannot initialize function microcode state at %a\n", cdg.insn.ea);
    return MERR_BADARCH;
  }
  if ( cdg.insn.ea == state_entry )
  {
    netnode wasm_node(wasmhex_node_name, 0, false);
    qstring startup_helper;
    if ( wasm_node != BADNODE && wasm_node.supstr_ea(&startup_helper, state_entry, 'R') > 0 )
    {
      const uchar no_argument_types[] = { 0 };
      const merror_t startup_error
        = emit_call(cdg, startup_helper.c_str(), 0, 0x40, false, no_argument_types);
      if ( startup_error != MERR_OK )
        return startup_error;
      unreachable = false;
      return MERR_OK;
    }
  }
  prune_closed_controls(cdg.insn.ea);
  for ( size_t i = controls.size(); i > 1; --i )
  {
    wasm_control_frame_t &frame = controls[i - 1];
    if ( frame.kind == 0x04 && !frame.in_else && cdg.insn.ea < frame.end )
    {
      qvector<ea_t> if_targets;
      if ( get_ea_list(frame.start, 'C', &if_targets) && if_targets.size() == 1
           && cdg.insn.ea > if_targets[0] )
        frame.in_else = true;
    }
  }
  for ( size_t i = 0; i < resume_states.size(); ++i )
    if ( resume_states[i].target == cdg.insn.ea )
    {
      if ( unreachable )
      {
        stack = resume_states[i].stack;
        unreachable = false;
      }
      resume_states.erase(resume_states.begin() + i);
      break;
    }
  // The branch-stub lowerings (0xF2 branch_edge / 0xFF branch_goto)
  // are retired together with the memory-check 0xF9 (nothing synthesizes
  // those bytes any more). Reaching any of them means a stale database
  // predating the retirement; fail closed with a reason.
  if ( cdg.insn.itype == wasmhex_branch_edge || cdg.insn.itype == wasmhex_branch_goto
       || cdg.insn.itype == wasmhex_memory_bounds_check )
  {
    msg("WASMHEX_F5_UNSUPPORTED: retired synthetic itype %d at %a "
        "(reload the module with the S4c loader)\n",
        int(cdg.insn.itype), cdg.insn.ea);
    return MERR_BADARCH;
  }
  if ( cdg.insn.itype == wasmhex_call_dispatch_test
       || cdg.insn.itype == wasmhex_call_dispatch_default
       || cdg.insn.itype == wasmhex_call_dispatch_target
       || cdg.insn.itype == wasmhex_call_dispatch_null
       || cdg.insn.itype == wasmhex_call_dispatch_type
       || cdg.insn.itype == wasmhex_call_dispatch_bounds )
  {
    msg("WASMHEX_F5_UNSUPPORTED: retired call-dispatch stub itype %d at %a "
        "(reload the module with the S4b loader)\n",
        int(cdg.insn.itype), cdg.insn.ea);
    return MERR_BADARCH;
  }
  if ( cdg.insn.itype == wasmhex_memory_trap )
    return emit_memory_trap(cdg);
  if ( unreachable && cdg.insn.itype != wasmhex_end && cdg.insn.itype != wasmhex_else
       && cdg.insn.itype != wasmhex_catch && cdg.insn.itype != wasmhex_block
       && cdg.insn.itype != wasmhex_loop && cdg.insn.itype != wasmhex_if
       && cdg.insn.itype != wasmhex_br_table )
    return MERR_OK;
  if ( cdg.insn.itype >= wasmhex_v128_load8_lane && cdg.insn.itype <= wasmhex_v128_store64_lane )
    return emit_v128_lane_memory(cdg);
  if ( cdg.insn.itype == wasmhex_v128_load )
    return emit_v128_memory(cdg, false, 16);
  if ( cdg.insn.itype == wasmhex_v128_store )
    return emit_v128_memory(cdg, true, 16);
  if ( cdg.insn.itype == wasmhex_v128_load32_zero )
    return emit_v128_memory(cdg, false, 4);
  if ( cdg.insn.itype == wasmhex_v128_load64_zero )
    return emit_v128_memory(cdg, false, 8);
  if ( cdg.insn.itype == wasmhex_v128_load8_splat )
    return emit_v128_memory(cdg, false, 1, true);
  if ( cdg.insn.itype == wasmhex_v128_load16_splat )
    return emit_v128_memory(cdg, false, 2, true);
  if ( cdg.insn.itype == wasmhex_v128_load32_splat )
    return emit_v128_memory(cdg, false, 4, true);
  if ( cdg.insn.itype == wasmhex_v128_load64_splat )
    return emit_v128_memory(cdg, false, 8, true);
  if ( cdg.insn.itype >= wasmhex_v128_load8x8_s && cdg.insn.itype <= wasmhex_v128_load32x2_u )
    return emit_v128_memory(cdg, false, 8);
  if ( cdg.insn.itype >= wasmhex_i32_load && cdg.insn.itype <= wasmhex_i64_store32 )
    return emit_memory(cdg);
  if ( cdg.insn.itype >= wasmhex_f32_abs && cdg.insn.itype <= wasmhex_f64_copysign )
  {
    const bool is_f32 = cdg.insn.itype <= wasmhex_f32_copysign;
    const uchar float_type = is_f32 ? 0x7D : 0x7C;
    const uchar bits_type = is_f32 ? 0x7F : 0x7E;
    const int argument_count
      = cdg.insn.itype == wasmhex_f32_copysign || cdg.insn.itype == wasmhex_f64_copysign ? 2 : 1;
    return emit_float_bits_helper(cdg, argument_count, float_type, bits_type, true);
  }
  if ( cdg.insn.itype >= wasmhex_f32_eq && cdg.insn.itype <= wasmhex_f64_ge )
  {
    // Wasm float compares are ordered IEEE comparisons and never
    // signal; the boolean result does not depend on the NaN payload, so
    // they lower to a direct fp setcc on the fp operand registers. The
    // former wasm_fXX_cc_bits() helpers forced the operands through integer
    // bits (kreg round-trip) and left COERCE_FLOAT/COERCE_DOUBLE in the
    // pseudocode.
    static constexpr mcode_t compare_opcodes[] = {
      m_setz, m_setnz, m_setb, m_seta, m_setbe, m_setae,
    };
    const bool is_f32 = cdg.insn.itype <= wasmhex_f32_ge;
    const size_t index = size_t(cdg.insn.itype) - size_t(is_f32 ? wasmhex_f32_eq : wasmhex_f64_eq);
    return emit_compare(cdg, is_f32 ? uchar(0x7D) : uchar(0x7C), compare_opcodes[index], true);
  }
  if ( cdg.insn.itype >= wasmhex_f32_ceil && cdg.insn.itype <= wasmhex_f64_sqrt )
  {
    const uchar type = cdg.insn.itype <= wasmhex_f32_sqrt ? 0x7D : 0x7C;
    return emit_float_bits_helper(cdg, 1, type, type == 0x7D ? 0x7F : 0x7E, true);
  }
  if ( cdg.insn.itype >= wasmhex_f32_min && cdg.insn.itype <= wasmhex_f64_max )
  {
    const uchar type = cdg.insn.itype <= wasmhex_f32_max ? 0x7D : 0x7C;
    return emit_float_bits_helper(cdg, 2, type, type == 0x7D ? 0x7F : 0x7E, true);
  }
  if ( cdg.insn.itype >= wasmhex_i32_trunc_sat_f32_s
       && cdg.insn.itype <= wasmhex_i64_trunc_sat_f64_u )
  {
    static constexpr uchar float_types[] = {
      0x7D, 0x7D, 0x7C, 0x7C, 0x7D, 0x7D, 0x7C, 0x7C,
    };
    const size_t index = size_t(cdg.insn.itype - wasmhex_i32_trunc_sat_f32_s);
    const uchar result_type = index < 4 ? 0x7F : 0x7E;
    return emit_float_bits_helper(cdg, 1, float_types[index], result_type, false);
  }
  if ( cdg.insn.itype >= wasmhex_i32_trunc_f32_s && cdg.insn.itype <= wasmhex_i64_trunc_f64_u )
  {
    static constexpr uchar float_types[] = {
      0x7D, 0x7D, 0x7C, 0x7C, 0x7D, 0x7D, 0x7C, 0x7C,
    };
    const size_t index = size_t(cdg.insn.itype - wasmhex_i32_trunc_f32_s);
    const uchar result_type = index < 4 ? 0x7F : 0x7E;
    const uchar argument_types[] = { float_types[index] };
    return emit_typed_helper_from_instruction(cdg, 1, argument_types, result_type, false,
                                              "wasm_trap_");
  }
  if ( cdg.insn.itype >= wasmhex_i32_reinterpret_f32
       && cdg.insn.itype <= wasmhex_f64_reinterpret_i64 )
  {
    static constexpr uchar argument_types[] = { 0x7D, 0x7C, 0x7F, 0x7E };
    static constexpr uchar result_types[] = { 0x7F, 0x7E, 0x7D, 0x7C };
    const size_t index = size_t(cdg.insn.itype - wasmhex_i32_reinterpret_f32);
    return emit_typed_helper_from_instruction(cdg, 1, &argument_types[index], result_types[index],
                                              true);
  }
  switch ( cdg.insn.itype )
  {
    case wasmhex_unreachable: return emit_unreachable(cdg);
    case wasmhex_nop: return MERR_OK;
    case wasmhex_drop: return emit_drop(cdg);
    case wasmhex_select: return emit_select(cdg);
    case wasmhex_select_typed: return emit_select(cdg, uchar(cdg.insn.Op1.value));
    case wasmhex_end: return emit_end(cdg);
    case wasmhex_block: return emit_control_start(cdg, 0x02);
    case wasmhex_loop: return emit_control_start(cdg, 0x03);
    case wasmhex_if: return emit_if(cdg);
    case wasmhex_try: return emit_control_start(cdg, 0x06);
    case wasmhex_catch: return emit_catch(cdg);
    case wasmhex_throw: return emit_throw(cdg);
    case wasmhex_else: return emit_else(cdg);
    case wasmhex_br: return emit_br(cdg, false);
    case wasmhex_br_if: return emit_br(cdg, true);
    case wasmhex_br_on_null: return emit_br_on_reference(cdg, true);
    case wasmhex_br_on_non_null: return emit_br_on_reference(cdg, false);
    case wasmhex_br_table: return emit_br_table(cdg);
    case wasmhex_return: return emit_return(cdg);
    case wasmhex_i32_const: return emit_constant(cdg, 0x7F);
    // shortcut: bit-count helpers stay opaque until their WebAssembly semantics are defined in the IDB.
    case wasmhex_i32_clz: return emit_call(cdg, "wasm_i32_clz", 1);
    case wasmhex_i32_ctz: return emit_call(cdg, "wasm_i32_ctz", 1);
    case wasmhex_i32_popcnt: return emit_call(cdg, "wasm_i32_popcnt", 1);
    case wasmhex_i64_clz: return emit_call(cdg, "wasm_i64_clz", 1, 0x7E);
    case wasmhex_i64_ctz: return emit_call(cdg, "wasm_i64_ctz", 1, 0x7E);
    case wasmhex_i64_popcnt: return emit_call(cdg, "wasm_i64_popcnt", 1, 0x7E);
    case wasmhex_i32_eqz: return emit_eqz(cdg, 0x7F);
    case wasmhex_i32_eq: return emit_compare(cdg, 0x7F, m_setz);
    case wasmhex_i32_ne: return emit_compare(cdg, 0x7F, m_setnz);
    case wasmhex_i32_lt_s: return emit_compare(cdg, 0x7F, m_setl);
    case wasmhex_i32_lt_u: return emit_compare(cdg, 0x7F, m_setb);
    case wasmhex_i32_gt_s: return emit_compare(cdg, 0x7F, m_setg);
    case wasmhex_i32_gt_u: return emit_compare(cdg, 0x7F, m_seta);
    case wasmhex_i32_le_s: return emit_compare(cdg, 0x7F, m_setle);
    case wasmhex_i32_le_u: return emit_compare(cdg, 0x7F, m_setbe);
    case wasmhex_i32_ge_s: return emit_compare(cdg, 0x7F, m_setge);
    case wasmhex_i32_ge_u: return emit_compare(cdg, 0x7F, m_setae);
    case wasmhex_i64_eqz: return emit_eqz(cdg, 0x7E);
    case wasmhex_i64_eq: return emit_compare(cdg, 0x7E, m_setz);
    case wasmhex_i64_ne: return emit_compare(cdg, 0x7E, m_setnz);
    case wasmhex_i64_lt_s: return emit_compare(cdg, 0x7E, m_setl);
    case wasmhex_i64_lt_u: return emit_compare(cdg, 0x7E, m_setb);
    case wasmhex_i64_gt_s: return emit_compare(cdg, 0x7E, m_setg);
    case wasmhex_i64_gt_u: return emit_compare(cdg, 0x7E, m_seta);
    case wasmhex_i64_le_s: return emit_compare(cdg, 0x7E, m_setle);
    case wasmhex_i64_le_u: return emit_compare(cdg, 0x7E, m_setbe);
    case wasmhex_i64_ge_s: return emit_compare(cdg, 0x7E, m_setge);
    case wasmhex_i64_ge_u: return emit_compare(cdg, 0x7E, m_setae);
    case wasmhex_i64_const: return emit_constant(cdg, 0x7E);
    case wasmhex_f32_const: return emit_constant(cdg, 0x7D);
    case wasmhex_f64_const: return emit_constant(cdg, 0x7C);
    case wasmhex_local_get: return emit_local(cdg, true, false);
    case wasmhex_local_set: return emit_local(cdg, false, false);
    case wasmhex_local_tee: return emit_local(cdg, false, true);
    case wasmhex_global_get: return emit_global(cdg, false);
    case wasmhex_global_set: return emit_global(cdg, true);
    case wasmhex_ref_null: return emit_ref_null(cdg);
    case wasmhex_ref_is_null: return emit_ref_is_null(cdg);
    case wasmhex_ref_eq: return emit_ref_eq(cdg);
    case wasmhex_ref_func: return emit_ref_func(cdg);
    case wasmhex_ref_as_non_null: return emit_ref_as_non_null(cdg);
    case wasmhex_ref_i31: return emit_ref_i31(cdg);
    case wasmhex_i31_get_s: return emit_i31_get(cdg, true);
    case wasmhex_i31_get_u: return emit_i31_get(cdg, false);
    case wasmhex_i32_add: return emit_add(cdg, 0x7F);
    case wasmhex_i32_sub: return emit_integer_binary(cdg, 0x7F, m_sub);
    case wasmhex_i32_mul: return emit_integer_binary(cdg, 0x7F, m_mul);
    // shortcut: divide-by-zero and signed-overflow traps stay in helpers until trap lowering exists.
    case wasmhex_i32_div_s: return emit_call(cdg, "wasm_i32_div_s");
    case wasmhex_i32_div_u: return emit_call(cdg, "wasm_i32_div_u");
    case wasmhex_i32_rem_s: return emit_call(cdg, "wasm_i32_rem_s");
    case wasmhex_i32_rem_u: return emit_call(cdg, "wasm_i32_rem_u");
    // shortcut: preserve shift-count masking in named helpers until a safe direct shift form is available.
    case wasmhex_i32_shl: return emit_i32_shift(cdg, m_shl);
    case wasmhex_i32_shr_s: return emit_i32_shift(cdg, m_sar);
    case wasmhex_i32_shr_u: return emit_i32_shift(cdg, m_shr);
    case wasmhex_i32_rotl: return emit_call(cdg, "wasm_i32_rotl", 2);
    case wasmhex_i32_rotr: return emit_call(cdg, "wasm_i32_rotr", 2);
    case wasmhex_i32_and: return emit_integer_binary(cdg, 0x7F, m_and);
    case wasmhex_i32_or: return emit_integer_binary(cdg, 0x7F, m_or);
    case wasmhex_i32_xor: return emit_integer_binary(cdg, 0x7F, m_xor);
    case wasmhex_i64_add: return emit_add(cdg, 0x7E);
    case wasmhex_i64_sub: return emit_integer_binary(cdg, 0x7E, m_sub);
    case wasmhex_i64_mul: return emit_integer_binary(cdg, 0x7E, m_mul);
    // shortcut: division and remainder traps stay in typed helpers until trap lowering exists.
    case wasmhex_i64_div_s: return emit_call(cdg, "wasm_i64_div_s", 2, 0x7E);
    case wasmhex_i64_div_u: return emit_call(cdg, "wasm_i64_div_u", 2, 0x7E);
    case wasmhex_i64_rem_s: return emit_call(cdg, "wasm_i64_rem_s", 2, 0x7E);
    case wasmhex_i64_rem_u: return emit_call(cdg, "wasm_i64_rem_u", 2, 0x7E);
    case wasmhex_i64_and: return emit_integer_binary(cdg, 0x7E, m_and);
    case wasmhex_i64_or: return emit_integer_binary(cdg, 0x7E, m_or);
    case wasmhex_i64_xor: return emit_integer_binary(cdg, 0x7E, m_xor);
    // shortcut: preserve shift-count masking in named helpers until a safe direct shift form is available.
    case wasmhex_i64_shl: return emit_i64_shift(cdg, m_shl);
    case wasmhex_i64_shr_s: return emit_i64_shift(cdg, m_sar);
    case wasmhex_i64_shr_u: return emit_i64_shift(cdg, m_shr);
    case wasmhex_i64_rotl: return emit_call(cdg, "wasm_i64_rotl", 2, 0x7E);
    case wasmhex_i64_rotr: return emit_call(cdg, "wasm_i64_rotr", 2, 0x7E);
    case wasmhex_f32_add: return emit_add(cdg, 0x7D);
    case wasmhex_f32_sub: return emit_float_binary(cdg, 0x7D, m_fsub);
    case wasmhex_f32_mul: return emit_float_binary(cdg, 0x7D, m_fmul);
    case wasmhex_f32_div: return emit_float_binary(cdg, 0x7D, m_fdiv);
    case wasmhex_f64_add: return emit_add(cdg, 0x7C);
    case wasmhex_f64_sub: return emit_float_binary(cdg, 0x7C, m_fsub);
    case wasmhex_f64_mul: return emit_float_binary(cdg, 0x7C, m_fmul);
    case wasmhex_f64_div: return emit_float_binary(cdg, 0x7C, m_fdiv);
    case wasmhex_i32_wrap_i64: return emit_integer_conversion(cdg, 0x7E, 0x7F, m_low);
    case wasmhex_i64_extend_i32_s: return emit_integer_conversion(cdg, 0x7F, 0x7E, m_xds);
    case wasmhex_i64_extend_i32_u: return emit_integer_conversion(cdg, 0x7F, 0x7E, m_xdu);
    case wasmhex_i32_extend8_s: return emit_integer_conversion(cdg, 0x7F, 0x7F, m_xds, 1);
    case wasmhex_i32_extend16_s: return emit_integer_conversion(cdg, 0x7F, 0x7F, m_xds, 2);
    case wasmhex_i64_extend8_s: return emit_integer_conversion(cdg, 0x7E, 0x7E, m_xds, 1);
    case wasmhex_i64_extend16_s: return emit_integer_conversion(cdg, 0x7E, 0x7E, m_xds, 2);
    case wasmhex_i64_extend32_s: return emit_integer_conversion(cdg, 0x7E, 0x7E, m_xds, 4);
    case wasmhex_f32_convert_i32_s: return emit_float_conversion(cdg, 0x7F, 0x7D, m_i2f);
    case wasmhex_f32_convert_i32_u: return emit_float_conversion(cdg, 0x7F, 0x7D, m_u2f);
    case wasmhex_f32_convert_i64_s: return emit_float_conversion(cdg, 0x7E, 0x7D, m_i2f);
    case wasmhex_f32_convert_i64_u: return emit_float_conversion(cdg, 0x7E, 0x7D, m_u2f);
    case wasmhex_f32_demote_f64: return emit_float_conversion(cdg, 0x7C, 0x7D, m_f2f);
    case wasmhex_f64_convert_i32_s: return emit_float_conversion(cdg, 0x7F, 0x7C, m_i2f);
    case wasmhex_f64_convert_i32_u: return emit_float_conversion(cdg, 0x7F, 0x7C, m_u2f);
    case wasmhex_f64_convert_i64_s: return emit_float_conversion(cdg, 0x7E, 0x7C, m_i2f);
    case wasmhex_f64_convert_i64_u: return emit_float_conversion(cdg, 0x7E, 0x7C, m_u2f);
    case wasmhex_f64_promote_f32: return emit_float_conversion(cdg, 0x7D, 0x7C, m_f2f);
    case wasmhex_call: return emit_call(cdg);
    case wasmhex_call_unwind:
      return emit_call(cdg, nullptr, 2, 0x7F, false, nullptr, false, false, false, true);
    case wasmhex_memory_size: return emit_memory_size_grow(cdg, false);
    case wasmhex_memory_grow: return emit_memory_size_grow(cdg, true);
    case wasmhex_table_size: return emit_table_size(cdg);
    case wasmhex_memory_copy: return emit_memory_bulk(cdg, true);
    case wasmhex_memory_fill: return emit_memory_bulk(cdg, false);
    case wasmhex_memory_init: return emit_memory_init(cdg);
    case wasmhex_atomic: return emit_atomic(cdg);
    case wasmhex_data_drop: return emit_data_drop(cdg);
    case wasmhex_call_indirect: return emit_call(cdg, nullptr, 2, 0x7F, false, nullptr, true);
    case wasmhex_return_call_indirect: return emit_call(cdg, nullptr, 2, 0x7F, true, nullptr, true);
    case wasmhex_return_call: return emit_call(cdg, nullptr, 2, 0x7F, true);
    case wasmhex_call_ref:
    case wasmhex_return_call_ref:
    {
      // The reference dispatch chain hangs off the 'j' case blob at
      // the call source EA (no stub stream, no 'S' dispatcher EA). The
      // probe is memoized on the per-function cache.
      if ( probe_dispatch_blob(cdg.insn.ea) )
        return emit_ref_call_dispatch(cdg);
      const bool tail_call = cdg.insn.itype == wasmhex_return_call_ref;
      return emit_call(cdg, nullptr, 2, 0x7F, tail_call, nullptr, false, false, true);
    }
    default: return MERR_BADARCH;
  }
}

// Memoized 'j' existence probe — see filter.hpp. Outside a Go
// function (no cached FunctionIR) the probe goes straight to the netnode,
// exactly the previous behavior.
bool wasmhex_filter_t::probe_dispatch_blob(ea_t source)
{
  if ( active_function_ir != nullptr )
  {
    const auto memo = active_function_ir->dispatch_probe.find(source);
    if ( memo != active_function_ir->dispatch_probe.end() )
      return memo->second;
    netnode probe_node(wasmhex_node_name, 0, false);
    bytevec_t probe_blob;
    const bool present
      = probe_node != BADNODE && probe_node.getblob_ea(&probe_blob, source, 'j') > 0;
    active_function_ir->dispatch_probe.emplace(source, present);
    return present;
  }
  netnode probe_node(wasmhex_node_name, 0, false);
  if ( probe_node == BADNODE )
    return false;
  bytevec_t probe_blob;
  return probe_node.getblob_ea(&probe_blob, source, 'j') > 0;
}

// Total wall time this filter spent lowering the current
// function (WASMHEX_PHASE_TIMER=1). The ctree callback reads and resets it
// per function so the PHASE log can attribute filter time separately from
// the engine's optimizer/structuring passes.
double g_filter_apply_ms_total = 0.0;

merror_t wasmhex_filter_t::apply(codegen_t &cdg)
{
  struct phase_timer_guard_t
  {
    std::chrono::steady_clock::time_point t0;
    bool on = false;
    phase_timer_guard_t()
    {
      if ( options::debug("phases") )
      {
        on = true;
        t0 = std::chrono::steady_clock::now();
      }
    }
    ~phase_timer_guard_t()
    {
      if ( on )
        g_filter_apply_ms_total
          += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
               .count();
    }
  } phase_timer_guard;
  {
    if ( options::debug("mc") )
    {
      char ap_buf[112];
      int ap_n = qsnprintf(
        ap_buf, sizeof(ap_buf), "MC_APPLY ea=%a itype=%d op1=%u stksz=%d unr=%d\n", cdg.insn.ea,
        int(cdg.insn.itype), unsigned(cdg.insn.Op1.value), int(stack.size()), int(unreachable));
      if ( ap_n > 0 )
        ::write(2, ap_buf, ap_n);
    }
  }
  // C-frame fix: the abstract-stack pops below this instruction need the
  // EA for their fail messages (pop() has no codegen context).
  current_insn_ea = cdg.insn.ea;
  // Prune any scopes that ended before `cdg.insn.ea` BEFORE the Go-protocol
  // and direct-JMP handlers run (a single-byte `end` at the tail of an `if`
  // or `block` can be elided by IDA's flowchart when the block ended with an
  // unconditional branch, so `controls.size()` would otherwise stay 1 too
  // deep when `emit_go_direct_jmp` resolves `depth` in `apply()`).
  prune_closed_controls(cdg.insn.ea);
  // The resume-state kick (unreachable -> false, stack restore) must
  // run BEFORE the Go-protocol consumption handlers below — the first
  // consumed instruction after a br_table is usually the resume entry
  // itself, and swallowing it with `return MERR_OK` skipped the kick, so
  // every later instruction no-op'd under `unreachable` until the store
  // pop failed. apply_instruction repeats the check harmlessly (the
  // state is erased on the first hit).
  for ( size_t i = 0; i < resume_states.size(); ++i )
    if ( resume_states[i].target == cdg.insn.ea )
    {
      if ( unreachable )
      {
        stack = resume_states[i].stack;
        unreachable = false;
      }
      resume_states.erase(resume_states.begin() + i);
      break;
    }
  // Consume the `local.get 0` selector push immediately preceding the
  // replaced Go resume dispatcher br_table, and replace the dispatcher
  // br_table itself with a direct edge to the main entry block (PC_B == 0).
  if ( go_dispatch_ea != BADADDR )
  {
    if ( cdg.insn.itype == wasmhex_local_get && cdg.insn.ea == go_dispatch_selector_ea )
      return MERR_OK;
    if ( cdg.insn.itype == wasmhex_br_table && cdg.insn.ea == go_dispatch_ea )
      return emit_go_dispatcher_replacement(cdg);
  }
  // A recorded const-JMP `i32.const k;
  // local.set PC_B; br entryPointLoop` lowers to a direct edge, so the
  // PC_B constant and its local store are consumed as a stack-neutral
  // pair (the constant never pushes, the store never pops) and the br
  // emits the goto at emit_go_direct_jmp.
  if ( !go_jmps.empty() )
  {
    if ( cdg.insn.itype == wasmhex_i32_const )
    {
      if ( go_jmp_by_const.find(cdg.insn.ea) != go_jmp_by_const.end() )
        return MERR_OK; // consumed with its local.set below
    }
    else if ( cdg.insn.itype == wasmhex_local_set )
    {
      if ( go_jmp_by_set.find(cdg.insn.ea) != go_jmp_by_set.end() )
        return MERR_OK; // the consumed constant never pushed — nothing to pop
    }
    else if ( cdg.insn.itype == wasmhex_br )
    {
      const auto go_jmp = go_jmp_by_br.find(cdg.insn.ea);
      if ( go_jmp != go_jmp_by_br.end() )
        return emit_go_direct_jmp(cdg, go_jmps[go_jmp->second]);
    }
  }
  // The SP-decrement leg of a recorded Go call (local.get L;
  // i32.const K; i32.sub; local.tee L; global.set 0). Consumed as one
  // unit with the store/PC_B/refresh/br legs — keeping only the store leg
  // made the cached SP drift -8 per call. Nothing is emitted and the
  // abstract stack is left untouched (the leg is stack-neutral).
  if ( !go_call_sp_arith_eas.empty() && go_call_sp_arith_eas.count(cdg.insn.ea) != 0 )
    return MERR_OK;
  // The Go frame legs (case-0 prologue SP -= frame_size, epilogue
  // SP += frame_size + 8) are consumed stack-neutrally — the decompiler
  // frame IS the Go frame, so the pointer updates must not exist as code.
  // collect_go_frame_legs verified the exact five-instruction shapes.
  if ( go_frame_mode && go_frame_leg_eas.count(cdg.insn.ea) != 0 )
    return MERR_OK;
  // The PC_F extraction sequence (`i64.const 16; i64.shr_u;
  // i32.wrap_i64; local.set 0; local.get 0`) of a recorded Go indirect call
  // is consumed as stack-neutral so the 64-bit funcval target stays on top
  // of the abstract stack for `emit_call`.
  if ( !go_pcf_extract_eas.empty() && go_pcf_extract_eas.count(cdg.insn.ea) != 0 )
    return MERR_OK;
  // Consume the goroutine plumbing at protocol sites loaded
  // from blob 'x' — the return-address push store, the post-call SP
  // refresh pair, and the pause br_if. Pops still run so the abstract
  // stack stays balanced; nothing is emitted, so the presentation shows
  // the plain call and the inline resume body.
  const auto go_arg_store = go_call_arg_store_types.find(cdg.insn.ea);
  if ( go_arg_store != go_call_arg_store_types.end() && cdg.insn.itype >= wasmhex_i32_load
       && cdg.insn.itype <= wasmhex_i64_store32 )
  {
    // An argument store of a recorded Go call — consume the memory
    // write and KEEP the value on the stack; emit_call later renders it
    // as a call argument instead of a raw linear-memory store.
    wasm_value_t value, address;
    if ( !pop(go_arg_store->second, &value, true) || !pop(0x7F, &address, true) )
      return MERR_BADARCH;
    push(value.op, go_arg_store->second);
    // The kept value may be a virtual frame-address marker (a Go
    // `&stack_cell` argument word) — push() rebuilds the stack slot from
    // the mop alone, so restore the marker flags before emit_call pops it.
    if ( is_c_frame_marker(value) )
    {
      stack.back().is_c_frame_address = value.is_c_frame_address;
      stack.back().is_c_sp_base = value.is_c_sp_base;
      stack.back().c_frame_offset = value.c_frame_offset;
    }
    return MERR_OK;
  }
  if ( cdg.insn.itype == wasmhex_i32_const
       && std::binary_search(go_call_const_eas.begin(), go_call_const_eas.end(), cdg.insn.ea) )
  {
    // The i32.const 0 PC_B push right before a Go call whose
    // arguments were consumed above — the rendered signature carries the
    // real Go arguments, so this placeholder is skipped entirely (no
    // push, nothing to pop — it never existed for the abstract stack).
    return MERR_OK;
  }
  if ( cdg.insn.itype == wasmhex_i64_store
       && (go_empty_args_store_eas.count(cdg.insn.ea) != 0
           || go_window_fold_store_eas.count(cdg.insn.ea) != 0) )
  {
    // A proven single-use window store (an EMPTY
    // variadic []any fill, or a folded group cell whose reader became the
    // call's argument expression) — consumed with the call plumbing.
    wasm_value_t empty_value, empty_address;
    if ( !pop(0x7E, &empty_value, true) || !pop(0x7F, &empty_address, true) )
      return MERR_BADARCH;
    return MERR_OK;
  }
  if ( cdg.insn.itype == wasmhex_i64_store
       && std::binary_search(go_call_store_eas.begin(), go_call_store_eas.end(), cdg.insn.ea) )
  {
    wasm_value_t push_value, push_address;
    if ( !pop(0x7E, &push_value, true) || !pop(0x7F, &push_address, true) )
      return MERR_BADARCH;
    return MERR_OK;
  }
  if ( cdg.insn.itype == wasmhex_global_get
       && std::binary_search(go_call_sp_refresh_eas.begin(), go_call_sp_refresh_eas.end(),
                             cdg.insn.ea) )
  {
    return MERR_OK; // the refresh's global.get — consumed with its local.set below
  }
  if ( cdg.insn.itype == wasmhex_local_set )
  {
    const auto go_sp_set = go_call_sp_set_locals.find(cdg.insn.ea);
    if ( go_sp_set != go_call_sp_set_locals.end() )
    {
      // This is the shape-verified `local.set L` of a recorded SP
      // refresh — consumed as a stack-neutral pair with the global.get
      // above (nothing pushed, nothing popped). Keyed by EA and index, so
      // an unrelated local.set interleaved by the codegen order can never
      // be mistaken for the protocol's write.
      if ( uint32(cdg.insn.Op1.value) != go_sp_set->second )
      {
        msg("WASMHEX_INVARIANT: Go SP refresh local.set %a writes local %u "
            "but the verified protocol instance writes local %u\n",
            cdg.insn.ea, uint32(cdg.insn.Op1.value), go_sp_set->second);
        return MERR_BADARCH;
      }
      return MERR_OK;
    }
  }
  if ( cdg.insn.itype == wasmhex_br_if
       && std::binary_search(go_call_br_eas.begin(), go_call_br_eas.end(), cdg.insn.ea) )
  {
    wasm_value_t condition;
    if ( !pop(0x7F, &condition) )
      return MERR_BADARCH;
    if ( std::binary_search(go_call_noreturn_br_eas.begin(), go_call_noreturn_br_eas.end(),
                            cdg.insn.ea) )
    {
      // Terminate cold-tail panic/throw resume blocks (`block $bK` arms)
      // so consecutive cold blocks at the tail of the function do not chain
      // into a thousand-link fallthrough cascade. Inline nil-check guards
      // (`if (p == 0) { sigpanic(); }`) keep their `if` frame on top of
      // `controls` (`controls.back().kind == 0x04`), so they merge back into
      // the normal path instead of becoming a 2-way non-converging split.
      if ( !controls.empty() && controls.back().kind == 0x02 )
        return emit_unreachable(cdg);
    }
    return MERR_OK;
  }
  const merror_t result = apply_instruction(cdg);
  if ( result != MERR_OK )
  {
    msg("wasmhex: microcode lowering failed at %a (itype=%d, merror=%d)\n", cdg.insn.ea,
        int(cdg.insn.itype), int(result));
    clear_runtime_state();
  }
  return result;
}
}
