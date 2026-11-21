#include "module.hpp"
#include "../../backend/idb/metadata_schema/metadata.hpp"

#include <algorithm>
namespace wasmhex
{

// Jump-style crefs must be decodable, inside the code segment and inside
// the owning function (corrupted 'C' metadata poisons the decompiler
// flowchart); the one sanctioned cross-function exception is a proven Go
// resume dispatch. A rejected edge stores a versioned 'u' record at the
// entry EA and the F5 gate refuses the function — fail-closed, never
// silently invented flow. Instruction boundaries come from the shared
// decoder's linear stream, not a decode_insn probe (mid-instruction bytes
// can happen to decode).
static const qvector<ea_t> *wasmhex_boundary_cache(ea_t holder_start)
{
  // Single-entry cache: emulation processes one function at a time and
  // cross-function Go-dispatch targets resolve through the same holder.
  static ea_t cached_start = BADADDR;
  static bool cache_complete = false;
  static qvector<ea_t> boundaries;
  if ( cached_start != holder_start )
  {
    func_entry_info_t holder;
    if ( !get_func_entry_info(&holder, holder_start) )
      return nullptr;
    boundaries.clear();
    cache_complete = true;
    for ( ea_t ea = holder.start_ea; ea < holder.end_ea; )
    {
      boundaries.push_back(ea);
      insn_t insn;
      const int size = decode_insn(&insn, ea);
      if ( size <= 0 )
      {
        // The linear decode broke: boundaries beyond this point are
        // unknowable — the cache stays deliberately incomplete so every
        // later check for this holder fails closed (decode_failed).
        cache_complete = false;
        break;
      }
      ea += size;
    }
    cached_start = holder_start;
  }
  return cache_complete ? &boundaries : nullptr;
}

static void wasmhex_mark_cfg_incomplete(ea_t from, ea_t to, wasmhex_cfg_reject_t reason)
{
  msg("WASMHEX_F5_UNSUPPORTED: control edge %a -> %a rejected at %a (%s) — "
      "the function will fail closed\n",
      from, to, from, cfg_reject_reason(reason));
  const ea_t owner_start = get_func_start(from);
  if ( owner_start == BADADDR )
    return; // freestanding code cannot be gated — the message above stays
  store_cfg_reject(owner_start, reason, from, to);
}

// Boundary proof for a jump target: inside a function, the target must be
// an EA of that function's linear decode; outside every function only the
// provenance-checked Go dispatch path arrives (legacy probe retained).
static bool wasmhex_target_on_boundary(ea_t to, wasmhex_cfg_reject_t *reason)
{
  const ea_t holder_start = get_func_start(to);
  if ( holder_start == BADADDR )
  {
    insn_t probe;
    if ( decode_insn(&probe, to) > 0 && probe.size > 0 )
      return true;
    *reason = wasmhex_cfg_reject_t::not_boundary;
    return false;
  }
  const qvector<ea_t> *boundaries = wasmhex_boundary_cache(holder_start);
  if ( boundaries == nullptr )
  {
    *reason = wasmhex_cfg_reject_t::decode_failed;
    return false;
  }
  if ( !std::binary_search(boundaries->begin(), boundaries->end(), to) )
  {
    *reason = wasmhex_cfg_reject_t::not_boundary;
    return false;
  }
  return true;
}

static bool wasmhex_add_jump(ea_t from, ea_t to, bool go_dispatch = false)
{
  func_entry_info_t owner;
  const bool has_owner = get_func_entry_info(&owner, from);
  const bool cross_function = has_owner && (to < owner.start_ea || to >= owner.end_ea);
  // The old `to > UINT32_MAX` rejection is gone — .text lives
  // above 4GiB now, so every legitimate branch target exceeds it. The
  // cross-function and boundary checks below remain the real guards.
  if ( cross_function && !go_dispatch )
  {
    wasmhex_mark_cfg_incomplete(from, to, wasmhex_cfg_reject_t::cross_function);
    return false;
  }
  wasmhex_cfg_reject_t reason = wasmhex_cfg_reject_t::not_boundary;
  if ( !wasmhex_target_on_boundary(to, &reason) )
  {
    wasmhex_mark_cfg_incomplete(from, to, reason);
    return false;
  }
  add_cref(from, to, fl_JN);
  return true;
}

static bool wasmhex_br_table_targets(ea_t ea, qvector<ea_t> *targets)
{
  qvector<uint32> depths;
  if ( get_branch_table_metadata(ea, targets, &depths) )
    return !targets->empty();
  return get_ea_list(ea, 'C', targets) && !targets->empty();
}

ssize_t wasmhex_procmod_t::emulate_instruction(va_list va)
{
  const insn_t *insn = va_arg(va, const insn_t *);
  {
    netnode dref_node(wasmhex_node_name, 0, false);
    if ( dref_node != BADNODE )
    {
      ea_t dref_target = BADADDR;
      uchar dref_kind = 0;
      if ( dref_node.supval_ea(insn->ea, &dref_target, sizeof(dref_target), '8')
             == sizeof(dref_target)
           && dref_node.supval_ea(insn->ea, &dref_kind, sizeof(dref_kind), '9') == sizeof(dref_kind)
           && dref_target != BADADDR && dref_target != insn->ea )
      {
        add_dref(insn->ea, dref_target, dref_kind == 2 ? dr_W : dr_R);
      }
    }
  }
  qvector<ea_t> control_targets;
  if ( insn->itype == wasmhex_if || insn->itype == wasmhex_br_if
       || insn->itype == wasmhex_br_on_null || insn->itype == wasmhex_br_on_non_null )
  {
    if ( !get_ea_list(insn->ea, 'C', &control_targets) || control_targets.size() != 1 )
      return 0;
    ea_t branch_target = control_targets[0];
    if ( insn->itype == wasmhex_if )
    {
      ea_t end_ea = BADADDR;
      if ( !get_ea_value(insn->ea, 'X', &end_ea) )
        return 0;
      if ( branch_target < end_ea )
      {
        insn_t else_insn;
        if ( decode_insn(&else_insn, branch_target) <= 0 || else_insn.itype != wasmhex_else )
          return 0;
        branch_target += else_insn.size;
      }
    }
    // A false return already marked the owning function cfg-incomplete
    // with the rejection reason and both EAs — F5 refuses it there (no
    // silent CFG hole). The fallthrough edge below is real semantics for
    // the not-taken side and keeps the disassembly truthful.
    wasmhex_add_jump(insn->ea, branch_target);
    add_cref(insn->ea, insn->ea + insn->size, fl_F);
    return 1;
  }
  if ( insn->itype == wasmhex_else || insn->itype == wasmhex_br )
  {
    if ( !get_ea_list(insn->ea, 'C', &control_targets) || control_targets.size() != 1 )
      return 0;
    // False = function marked cfg-incomplete (reason + EAs recorded)
    // br has no fallthrough, so nothing else to emit here.
    wasmhex_add_jump(insn->ea, control_targets[0]);
    return 1;
  }
  if ( insn->itype == wasmhex_br_table )
  {
    // Jtbl is the only br_table lowering. No dispatcher
    // stubs exist in the stream - create the case crefs directly
    // (the last entry is the default) with no fallthrough.
    qvector<ea_t> jtbl_targets;
    qvector<uint32> jtbl_depths;
    if ( (get_branch_table_metadata(insn->ea, &jtbl_targets, &jtbl_depths)
          || get_ea_list(insn->ea, 'C', &jtbl_targets))
         && !jtbl_targets.empty() )
    {
      // A Go resume dispatch (`local.get PC_B; br_table` in
      // the entryPointLoop) legitimately fans out across function
      // boundaries. Trust it ONLY with the loader's byte-level Go
      // provenance AND the dispatch shape: the two bytes right before
      // this br_table are `local.get 0` (0x20 0x00) loading PC_B.
      const bool go_dispatch
        = load_go_module() && get_byte(insn->ea - 2) == 0x20 && get_byte(insn->ea - 1) == 0x00;
      for ( ea_t target : jtbl_targets )
      {
        // Every check for a case target (32-bit range, in-function
        // without Go provenance, linear-decode boundary) lives in
        // wasmhex_add_jump — one authority. A dropped case edge marks the
        // owning function cfg-incomplete with the reason and EAs; the case
        // itself contributes no cref (fail-closed, never a half-known
        // table).
        wasmhex_add_jump(insn->ea, target, go_dispatch);
      }
      return 1; // no fallthrough: every case leaves the block
    }
    return 0;
  }
  if ( insn->itype == wasmhex_call_indirect )
  {
    netnode wasm_node(wasmhex_node_name, 0, false);
    bytevec_t dispatch_probe;
    if ( wasm_node != BADNODE && wasm_node.getblob_ea(&dispatch_probe, insn->ea, 'j') > 0 )
    {
      qvector<uint32> blob;
      if ( !read_dispatch_cases(wasm_node, insn->ea, &blob) )
      {
        msg("WASMHEX_F5_UNSUPPORTED: malformed call dispatch case blob at %a\n", insn->ea);
        return 0;
      }
      const size_t case_count = blob.size() / 3;
      for ( size_t k = 0; k < case_count; ++k )
      {
        if ( blob[k * 3 + 2] != 0 ) // trap case: no call target
          continue;
        const uint32 function_index = blob[k * 3 + 1];
        const ea_t target = wasm_node.eaget_idx(function_index, 'F');
        if ( target != BADADDR && target != 0 )
          add_cref(insn->ea, target, fl_CN);
        else
        {
          qstring imported_name;
          if ( wasm_node.supstr(&imported_name, function_index, 'H') <= 0 )
          {
            msg("WASMHEX_F5_UNSUPPORTED: dispatch case %u has no callee at %a\n", unsigned(k),
                insn->ea);
            return 0;
          }
        }
      }
      add_cref(insn->ea, insn->ea + insn->size, fl_F);
      return 1;
    }
    // No dispatch blob: the generic resolved/checked indirect paths
    // below own this instruction.
  }
  if ( insn->itype == wasmhex_throw )
  {
    ea_t catch_ea = BADADDR;
    if ( !get_ea_value(insn->ea, 'Y', &catch_ea) || catch_ea == BADADDR )
      return 0;
    // False = function marked cfg-incomplete with reason + EAs.
    wasmhex_add_jump(insn->ea, catch_ea);
    return 1;
  }
  if ( insn->itype == wasmhex_return || (insn->itype == wasmhex_end && is_function_end(insn->ea)) )
    return 1;
  if ( insn->itype == wasmhex_call || insn->itype == wasmhex_call_unwind
       || insn->itype == wasmhex_return_call )
  {
    const uint32 function_index = uint32(insn->Op1.value);
    netnode wasm_node(wasmhex_node_name, 0, false);
    const ea_t target = wasm_node == BADNODE ? BADADDR : wasm_node.eaget_idx(function_index, 'F');
    if ( target != BADADDR && target != 0 )
      add_cref(insn->ea, target, fl_CN);
    if ( insn->itype == wasmhex_call_unwind )
    {
      ea_t catch_ea = BADADDR;
      if ( wasm_node == BADNODE
           || wasm_node.supval_ea(insn->ea, &catch_ea, sizeof(catch_ea), 'Y') != sizeof(catch_ea)
           || catch_ea == BADADDR )
        return 0;
      // False = function marked cfg-incomplete with reason + EAs
      // the call cref above already exists, only the unwind edge is gone.
      wasmhex_add_jump(insn->ea, catch_ea);
    }
  }
  else if ( insn->itype == wasmhex_return_call_indirect )
  {
    netnode wasm_node(wasmhex_node_name, 0, false);
    if ( wasm_node == BADNODE )
      return 0;
    wasmhex_call_trap_t trap_reason = wasmhex_call_trap_t::none;
    if ( wasm_node.supval_ea(insn->ea, &trap_reason, sizeof(trap_reason), 'Z')
         == sizeof(trap_reason) )
      return trap_reason == wasmhex_call_trap_t::none ? 0 : 1;
    wasmhex_sparse_dispatch_t sparse_dispatch = {};
    uint32 case_count = 0;
    if ( wasm_node.supval_ea(insn->ea, &sparse_dispatch, sizeof(sparse_dispatch), 'J')
           == sizeof(sparse_dispatch)
         && get_dispatch_case_count(wasm_node, insn->ea, &case_count)
         && sparse_dispatch.table_size != 0 && case_count <= WASMHEX_MAX_SPARSE_TABLE_DISPATCH_CASES
         && case_count <= sparse_dispatch.table_size && sparse_dispatch.target_count <= case_count )
    {
      // Tail dispatch xrefs from the packed 'j' case blob (no stub
      // stream to jump into anymore).
      bytevec_t dispatch_probe;
      if ( wasm_node.getblob_ea(&dispatch_probe, insn->ea, 'j') > 0 )
      {
        qvector<uint32> blob;
        if ( !read_dispatch_cases(wasm_node, insn->ea, &blob) )
        {
          msg("WASMHEX_F5_UNSUPPORTED: malformed tail dispatch case blob at %a\n", insn->ea);
          return 0;
        }
        const size_t tail_case_count = blob.size() / 3;
        for ( size_t k = 0; k < tail_case_count; ++k )
        {
          if ( blob[k * 3 + 2] != 0 ) // trap case
            continue;
          const ea_t target = wasm_node.eaget_idx(blob[k * 3 + 1], 'F');
          if ( target != BADADDR && target != 0 )
            add_cref(insn->ea, target, fl_CN);
        }
        return 1;
      }
      msg("WASMHEX_F5_UNSUPPORTED: tail dispatch case blob missing at %a\n", insn->ea);
      return 0;
    }
    checked_table_call_t checked_call = {};
    if ( wasm_node.supval_ea(insn->ea, &checked_call, sizeof(checked_call), 'K')
           == sizeof(checked_call)
         && checked_call.table_index == uint32(insn->Op2.value)
         && checked_call.type_index == uint32(insn->Op1.value) && checked_call.table_size != 0 )
      return 1;
    uint32 function_index = 0;
    if ( wasm_node.supval_ea(insn->ea, &function_index, sizeof(function_index), 'G')
         != sizeof(function_index) )
      return 0;
    const ea_t target = wasm_node.eaget_idx(function_index, 'F');
    if ( target != BADADDR && target != 0 )
      add_cref(insn->ea, target, fl_CN);
    else
    {
      qstring imported_name;
      if ( wasm_node.supstr(&imported_name, function_index, 'H') <= 0 )
        return 0;
    }
    return 1;
  }
  else if ( insn->itype == wasmhex_call_ref || insn->itype == wasmhex_return_call_ref )
  {
    uint32 function_index = 0;
    netnode wasm_node(wasmhex_node_name, 0, false);
    if ( wasm_node == BADNODE )
      return 0;
    if ( wasm_node.supval_ea(insn->ea, &function_index, sizeof(function_index), 'G')
         == sizeof(function_index) )
    {
      const ea_t target = wasm_node.eaget_idx(function_index, 'F');
      if ( target != BADADDR && target != 0 )
        add_cref(insn->ea, target, fl_CN);
      else
      {
        qstring imported_name;
        if ( wasm_node.supstr(&imported_name, function_index, 'H') <= 0 )
          return 0;
      }
    }
    else
    {
      // Reference dispatch — xrefs from the packed 'j' case blob
      // (no 'S' dispatcher EA exists without a stub stream).
      bytevec_t dispatch_probe;
      if ( wasm_node.getblob_ea(&dispatch_probe, insn->ea, 'j') > 0 )
      {
        qvector<uint32> blob;
        if ( !read_dispatch_cases(wasm_node, insn->ea, &blob) )
        {
          msg("WASMHEX_F5_UNSUPPORTED: malformed reference dispatch case blob at %a\n", insn->ea);
          return 0;
        }
        const size_t ref_case_count = blob.size() / 3;
        for ( size_t k = 0; k < ref_case_count; ++k )
        {
          if ( blob[k * 3 + 2] != 0 ) // trap case
            continue;
          const ea_t target = wasm_node.eaget_idx(blob[k * 3 + 1], 'F');
          if ( target != BADADDR && target != 0 )
            add_cref(insn->ea, target, fl_CN);
        }
        if ( insn->itype == wasmhex_call_ref )
          add_cref(insn->ea, insn->ea + insn->size, fl_F);
        return 1;
      }
      msg("WASMHEX_F5_UNSUPPORTED: call_ref has no resolved callee or "
          "dispatch blob at %a\n",
          insn->ea);
      return 0;
    }
  }
  bool statically_trapping_call = false;
  if ( insn->itype == wasmhex_call_indirect || insn->itype == wasmhex_return_call_indirect )
  {
    wasmhex_call_trap_t trap_reason = wasmhex_call_trap_t::none;
    netnode wasm_node(wasmhex_node_name, 0, false);
    if ( wasm_node != BADNODE
         && wasm_node.supval_ea(insn->ea, &trap_reason, sizeof(trap_reason), 'Z')
           == sizeof(trap_reason) )
    {
      if ( trap_reason == wasmhex_call_trap_t::none )
        return 0;
      statically_trapping_call = true;
    }
  }
  if ( (instructions[insn->itype].feature & CF_STOP) == 0 && !statically_trapping_call )
    add_cref(insn->ea, insn->ea + insn->size, fl_F);
  return 1;
}
}
