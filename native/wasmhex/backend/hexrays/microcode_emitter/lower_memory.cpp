#include "filter.hpp"
#include "../../../backend/hexrays/register_mapping/frame.hpp"
#include "../../../ida/loader/options.hpp"
#include <algorithm>
#include <bytes.hpp>
#include <cstring>
#include <segment.hpp>
namespace wasmhex
{
bool wasmhex_filter_t::is_c_frame_marker(const wasm_value_t &value)
{
  return value.is_c_frame_address || value.is_c_sp_base;
}

bool wasmhex_filter_t::is_table_base_marker(const wasm_value_t &value)
{
  return value.is_table_base;
}

bool wasmhex_filter_t::materialize_c_frame_marker(codegen_t &cdg, wasm_value_t *value)
{
  if ( !is_c_frame_marker(*value) )
    return true;
  // The pre-prologue stack pointer lives outside the recovered frame and
  // cannot be expressed; only fp-relative addresses materialize.
  if ( value->is_c_sp_base )
    return false;
  const int64 k = int64(value->c_frame_offset);
  if ( k < 0 || k >= int64(c_frame_metadata.c_frame_bytes) || k >= int64(cdg.mba->frsize) )
    return false;
  mop_t frame_slot;
  frame_slot.make_stkvar(cdg.mba, sval_t(k));
  frame_slot.size = NOSIZE;
  mop_addr_t *address = new mop_addr_t(frame_slot, NOSIZE, 4);
  if ( address == nullptr )
    return false;
  value->op.t = mop_a;
  value->op.a = address;
  value->op.size = 4;
  value->has_constant = false;
  value->is_c_frame_address = false;
  value->is_c_sp_base = false;
  return true;
}

merror_t wasmhex_filter_t::reject_c_frame_marker_use(codegen_t &cdg, const char *context)
{
  msg("WASMHEX_F5_UNSUPPORTED: C shadow stack marker used in %s at %a\n", context, cdg.insn.ea);
  return MERR_BADARCH;
}

// C-frame fix: resolve a C-frame marker to a usable scalar operand.
// In-frame offsets materialize as decompiler stack-slot addresses; every
// other position (sp base, negative offsets, past-the-end offsets — real
// C-ABI shapes such as call arguments at fp+framesize, pointer tagging,
// alloca results) resolves to the module's true runtime address,
// sp_global + offset. This is the exact C shadow stack semantics, not a
// repair: any marker position has a well-defined linear address.
bool wasmhex_filter_t::resolve_c_frame_scalar(codegen_t &cdg, wasm_value_t *value, size_t depth)
{
  if ( !is_c_frame_marker(*value) )
    return true;
  if ( materialize_c_frame_marker(cdg, value) )
    return true;
  mop_t raw;
  if ( lower_c_frame_marker_address(cdg, *value, depth, &raw) )
  {
    value->op = raw;
    value->is_c_frame_address = false;
    value->is_c_sp_base = false;
    value->has_constant = false;
    return true;
  }
  return false;
}

bool wasmhex_filter_t::widen_materialized_address(codegen_t &cdg, mop_t *op, int width,
                                                  size_t index)
{
  if ( op == nullptr || op->size != 4 || width != 8 || op->t == mop_f )
    return true; // nothing to do (already the right width / not scalar)
  mop_t wide;
  if ( !make_scratch_register(cdg, 8, index, &wide) )
    return false;
  if ( cdg.emit(m_xdu, op, nullptr, &wide) == nullptr )
    return false;
  *op = wide;
  return true;
}

// Lower a C-frame address marker to a plain linear-memory address
// operand. After the prologue the shadow stack pointer global equals fp,
// so any fp/sp-relative address is sp + offset. This is the coarse but
// sound fallback for offsets that fall OUTSIDE the recovered frame (where
// no decompiler stack slot exists): the access renders as an ordinary
// linear-memory deref, matching real wasm semantics.
bool wasmhex_filter_t::lower_c_frame_marker_address(codegen_t &cdg, const wasm_value_t &address,
                                                    size_t address_depth, mop_t *out)
{
  uchar sp_type = 0, sp_mutability = 0;
  ea_t sp_address = BADADDR;
  bool sp_imported = false;
  qstring sp_helper;
  if ( !get_global_metadata(c_frame_metadata.sp_global_index, &sp_type, &sp_mutability, &sp_address,
                            &sp_imported, &sp_helper)
       || sp_type != 0x7F )
    return false;
  int64 effective_offset = int64(address.c_frame_offset);
  if ( address.is_c_sp_base )
    effective_offset += c_frame_metadata.direction < 0 ? int64(c_frame_metadata.c_frame_bytes)
                                                       : -int64(c_frame_metadata.c_frame_bytes);
  mop_t base;
  if ( !make_operand_register(cdg, 0x7F, address_depth, &base) )
    return false;
  if ( sp_address != BADADDR )
  {
    mop_t sp_gvar;
    sp_gvar.make_gvar(sp_address);
    sp_gvar.size = 4;
    if ( !emit_move(cdg, sp_gvar, base, false) )
      return false;
  }
  else if ( sp_imported && !sp_helper.empty() )
  {
    qstring helper(sp_helper);
    helper.append("_get");
    const uchar argument_types[] = { 0x7F };
    if ( emit_call(cdg, helper.c_str(), 0, 0x7F, false, argument_types) != MERR_OK )
      return false;
    wasm_value_t sp_value;
    if ( !pop(0x7F, &sp_value) || !emit_move(cdg, sp_value.op, base, false) )
      return false;
  }
  else
    return false;
  if ( effective_offset == 0 )
  {
    *out = base;
    return true;
  }
  mop_t offset_mop, sum;
  offset_mop.make_number(uint64(int32(uint32(effective_offset))), 4, cdg.insn.ea, 0);
  if ( !make_scratch_register(cdg, 4, 1, &sum) )
    return false;
  if ( cdg.emit(m_add, &base, &offset_mop, &sum) == nullptr )
    return false;
  *out = sum;
  return true;
}

void wasmhex_filter_t::push_c_frame_address(int32 offset)
{
  wasm_value_t &value = stack.push_back();
  value.type = 0x7F;
  value.is_c_frame_address = true;
  value.c_frame_offset = offset;
}

merror_t wasmhex_filter_t::emit_go_receiver_member_load(codegen_t &cdg,
                                                        const memory_instruction_t &memory,
                                                        const wasm_value_t &address,
                                                        size_t address_depth)
{
  // The address operand is the 8-byte receiver pointer (+ its folded const
  // offset). The load's own memarg folds as one more 8-byte addend; the
  // m_ldx then reads the exact linear cell (wasmhex_memory_base == 0) with
  // a typed-pointer base the engine's struct-offset pass can name.
  mop_t effective_address = address.op;
  effective_address.size = 8;
  if ( cdg.insn.Op1.value != 0 )
  {
    mop_t addend;
    addend.make_number(uint64(uint32(cdg.insn.Op1.value)), 8, cdg.insn.ea, 0);
    mop_t sum;
    if ( !make_scratch_register(cdg, 8, 1, &sum) )
      return MERR_MEM;
    if ( cdg.emit(m_add, &effective_address, &addend, &sum) == nullptr )
      return MERR_INTERR;
    effective_address = sum;
  }
  const int result_size = wasm_type_size(memory.type);
  if ( result_size < memory.width )
    return MERR_BADARCH;
  mop_t selector;
  if ( !make_wasm_memory_selector(&selector, cdg.insn.ea, bounds_mode, false) )
    return MERR_BADARCH;
  mop_t loaded;
  if ( !(result_size == memory.width
           ? make_operand_register(cdg, memory.type, address_depth, &loaded)
           : make_scratch_register(cdg, memory.width, 1, &loaded)) )
    return MERR_MEM;
  if ( cdg.emit(m_ldx, &selector, &effective_address, &loaded) == nullptr )
    return MERR_INTERR;
  if ( result_size == memory.width )
  {
    push(loaded, memory.type);
    return MERR_OK;
  }
  const mcode_t conv = memory.signed_load ? m_xds : m_xdu;
  mop_t result;
  if ( !make_operand_register(cdg, memory.type, address_depth, &result) )
    return MERR_MEM;
  if ( cdg.emit(conv, &loaded, nullptr, &result) == nullptr )
    return MERR_INTERR;
  push(result, memory.type);
  return MERR_OK;
}

bool wasmhex_filter_t::fold_c_frame_arithmetic(codegen_t &cdg, const wasm_value_t &left,
                                               const wasm_value_t &right, bool subtract)
{
  if ( is_c_frame_marker(left) && is_c_frame_marker(right) )
  {
    (void)reject_c_frame_marker_use(cdg, "marker-to-marker arithmetic");
    return false;
  }
  if ( subtract && is_c_frame_marker(right) )
  {
    (void)reject_c_frame_marker_use(cdg, "right-hand side of i32.sub");
    return false;
  }
  const wasm_value_t &marker = is_c_frame_marker(left) ? left : right;
  const wasm_value_t &other = is_c_frame_marker(left) ? right : left;
  uint64 raw_const = other.constant;
  if ( !other.has_constant && !other.op.is_constant(&raw_const, false) )
  {
    // Non-constant offsets are handled by the caller through
    // materialize_c_frame_marker (fp+dynamic index) - no message here.
    return false;
  }
  const int64 delta = subtract ? -int64(int32(uint32(raw_const))) : int64(int32(uint32(raw_const)));
  if ( marker.is_c_sp_base )
  {
    if ( has_c_frame && soft_c_frame && !soft_frame_learned )
    {
      // Dataflow frame: no fixed prologue pattern - learn the
      // frame extent from the first constant sp adjustment instead.
      if ( delta == 0 )
      {
        push_c_frame_address(0);
        return true;
      }
      c_frame_metadata.c_frame_bytes = uint32(delta < 0 ? -delta : delta);
      c_frame_metadata.direction = delta < 0 ? -1 : 1;
      soft_frame_learned = true;
      push_c_frame_address(0);
      return true;
    }
    const int64 expected_delta = c_frame_metadata.direction < 0
      ? -int64(c_frame_metadata.c_frame_bytes)
      : int64(c_frame_metadata.c_frame_bytes);
    if ( !has_c_frame || c_frame_fp_written || delta != expected_delta )
    {
      (void)reject_c_frame_marker_use(cdg, "mismatched C stack prologue delta");
      return false;
    }
    push_c_frame_address(0);
    return true;
  }
  const int64 new_offset = int64(marker.c_frame_offset) + delta;
  if ( new_offset < INT32_MIN || new_offset > INT32_MAX )
  {
    (void)reject_c_frame_marker_use(cdg, "overflowing C frame offset arithmetic");
    return false;
  }
  // Keep the marker's operand type (the Go computed-address shape
  // folds at i64 before wrapping back to i32).
  wasm_value_t &folded = stack.push_back();
  folded.type = marker.type;
  folded.is_c_frame_address = true;
  folded.c_frame_offset = int32(new_offset);
  return true;
}

bool wasmhex_filter_t::fold_table_base_arithmetic(codegen_t &cdg, const wasm_value_t &left,
                                                  const wasm_value_t &right, bool subtract)
{
  if ( is_table_base_marker(left) && is_table_base_marker(right) )
    return false;
  if ( subtract && is_table_base_marker(right) )
    return false;
  const wasm_value_t &marker = is_table_base_marker(left) ? left : right;
  const wasm_value_t &other = is_table_base_marker(left) ? right : left;
  uint64 raw_const = other.constant;
  if ( !other.has_constant && !other.op.is_constant(&raw_const, false) )
    return false;

  const int64 delta = subtract ? -int64(int32(uint32(raw_const))) : int64(int32(uint32(raw_const)));
  const int64 new_offset = int64(marker.table_base_offset) + delta;
  if ( new_offset < 0 || new_offset > UINT32_MAX )
    return false;

  mop_t destination;
  if ( !make_operand_register(cdg, 0x7F, stack.size(), &destination) )
    return false;
  const mcode_t opcode = subtract ? m_sub : m_add;
  if ( cdg.emit(opcode, &left.op, &right.op, &destination) == nullptr )
    return false;

  push(destination, 0x7F);
  stack.back().is_table_base = true;
  stack.back().table_base_offset = uint32(new_offset);
  return true;
}

merror_t wasmhex_filter_t::emit_memory_trap(codegen_t &cdg)
{
  const uchar no_argument_types[] = { 0x7F };
  // __builtin_trap for the out-of-bounds trap as well.
  const merror_t error
    = emit_call(cdg, "__builtin_trap", 0, 0x40, false, no_argument_types, false, true);
  if ( error == MERR_OK )
    unreachable = true;
  else
    msg("WASMHEX_F5_UNSUPPORTED: could not emit memory out-of-bounds trap at %a\n", cdg.insn.ea);
  return error;
}

merror_t wasmhex_filter_t::emit_static_memory_trap(codegen_t &cdg)
{
  const merror_t error = emit_memory_trap(cdg);
  if ( error != MERR_OK )
    return error;
  if ( controls.empty() )
    return MERR_BADARCH;
  stack.resize(controls.back().base_stack.size());
  return MERR_OK;
}

merror_t wasmhex_filter_t::reject_memory64_addressing(codegen_t &cdg)
{
  msg("WASMHEX_F5_UNSUPPORTED: Memory64 direct access requires a fixed-memory proof at %a\n",
      cdg.insn.ea);
  return MERR_BADARCH;
}

merror_t wasmhex_filter_t::reject_unmapped_memory_extent(codegen_t &cdg)
{
  msg(
    "WASMHEX_F5_UNSUPPORTED: fixed WebAssembly linear-memory extent exceeds IDA's disjoint address range at %a\n",
    cdg.insn.ea);
  return MERR_BADARCH;
}

// Emit_memory_bounds_check (the 0xF9 decode-driven lowering) is
// retired — the parse never inserts 0xF9 bytes into .text and
// the memory bounds machinery lives in the memory lowerings now.
void wasmhex_filter_t::add_wasm_call_memory_clobber(codegen_t &cdg, ivlset_t *visible_memory,
                                                    mlist_t *spoiled) const
{
  const ivl_t glblow = cdg.mba->get_std_region(MMIDX_GLBLOW);
  if ( !glblow.empty() )
  {
    visible_memory->add(glblow);
    spoiled->add(glblow);
  }
  const ivl_t glbhigh = cdg.mba->get_std_region(MMIDX_GLBHIGH);
  if ( !glbhigh.empty() )
  {
    visible_memory->add(glbhigh);
    spoiled->add(glbhigh);
  }
  if ( has_c_frame && c_frame_metadata.c_frame_bytes != 0 )
  {
    const ivl_t lvars = cdg.mba->get_std_region(MMIDX_LVARS);
    if ( !lvars.empty() )
    {
      const asize_t c_bytes
        = std::min<asize_t>(lvars.size, asize_t(c_frame_metadata.c_frame_bytes));
      const ivl_t c_frame_ivl(lvars.off, c_bytes);
      visible_memory->add(c_frame_ivl);
      spoiled->add(c_frame_ivl);
    }
    else
    {
      spoiled->mem.add(0, asize_t(c_frame_metadata.c_frame_bytes));
    }
  }
}

merror_t wasmhex_filter_t::emit_growable_memory(codegen_t &cdg, const memory_instruction_t &memory,
                                                uchar memory_mode)
{
  const uchar address_type = memory_mode == 2 ? uchar(0x7E) : uchar(0x7F);
  wasm_value_t value, address;
  // In go-frame mode a store VALUE may be a frame-address marker
  // the Go `&stack_cell` escape (interface data word / slice backing
  // pointer) — materialized to &stkvar below.
  if ( memory.store
       && !pop(memory.type, &value, has_c_frame && (memory.type == 0x7F || go_frame_mode)) )
    return MERR_BADARCH;
  if ( memory.store && is_c_frame_marker(value) && !materialize_c_frame_marker(cdg, &value) )
  {
    // Storing an out-of-frame C-frame address degrades to the
    // sp-based linear-memory address instead of failing the function.
    mop_t raw_address;
    if ( !lower_c_frame_marker_address(cdg, value, stack.size(), &raw_address) )
      return reject_c_frame_marker_use(cdg, "store value out of frame");
    value.op = raw_address;
  }
  if ( memory.store && memory.type != 0x7D && memory.type != 0x7C
       && !widen_materialized_address(cdg, &value.op, wasm_type_size(memory.type), 2) )
    return MERR_MEM;
  if ( !pop(address_type, &address, has_c_frame && address_type == 0x7F) )
    return MERR_BADARCH;
  if ( is_c_frame_marker(address) )
    return emit_c_frame_scalar_memory(cdg, memory, address, value, stack.size());
  qstring helper;
  helper.sprnt("wasm_trap_memory_%s_width%u_signed%u_offset%llu_%s",
               memory.store ? "store" : "load", unsigned(memory.width),
               unsigned(memory.signed_load), (unsigned long long)cdg.insn.Op1.value,
               memory_mode == 2 ? "i64" : "i32");
  const uchar argument_types[] = { address_type, memory.type };
  if ( memory.store )
  {
    push(address.op, address_type);
    push(value.op, memory.type);
    return emit_call(cdg, helper.c_str(), 2, 0x40, false, argument_types);
  }
  push(address.op, address_type);
  return emit_call(cdg, helper.c_str(), 1, memory.type, false, argument_types);
}

bool wasmhex_filter_t::make_effective_memory_address(codegen_t &cdg, const wasm_value_t &address,
                                                     int address_size, uint64 offset_value,
                                                     uint64 access_width, uint64 memory_extent,
                                                     mop_t *effective_address,
                                                     bool *constant_address,
                                                     bool *statically_trapping)
{
  if ( effective_address == nullptr || constant_address == nullptr
       || statically_trapping == nullptr )
    return false;
  *statically_trapping = false;
  uint64 base_constant = address.constant;
  if ( address.has_constant || address.op.is_constant(&base_constant, false) )
  {
    *constant_address = true;
    const uint64 base = address_size == 4 ? uint64(uint32(base_constant)) : base_constant;
    const uint64 offset = address_size == 4 ? uint64(uint32(offset_value)) : offset_value;
    const uint64 sum = base + offset;
    if ( (address_size == 4 && sum > UINT32_MAX) || (address_size == 8 && sum < offset)
         || memory_extent < access_width || sum > memory_extent - access_width )
    {
      *statically_trapping = true;
      return true;
    }
    if ( resolve_data_address(sum, address_size, effective_address) )
    {
      *constant_address = true;
      return true;
    }
    mop_t constant_op;
    // On a wide (64-bit program) database the ldx/stx offset operand must
    // be 8 bytes regardless of the wasm address width or the accessed
    // segment's addressing — a 4-byte offset operand there fails
    // verification. Widen the constant instead.
    const int operand_size = inf_is_64bit() ? 8 : address_size;
    constant_op.make_number(uint64(wasmhex_memory_base) + sum, operand_size, cdg.insn.ea, 0);
    if ( !make_scratch_register(cdg, operand_size, 0, effective_address) )
      return false;
    minsn_t *move = cdg.emit(m_mov, &constant_op, nullptr, effective_address);
    if ( move == nullptr )
      return false;
    if ( bounds_mode == wasmhex_bounds_mode_t::hidden )
      move->clr_propagatable();
    return true;
  }
  *constant_address = false;
  const int operand_size = inf_is_64bit() ? 8 : address_size;
  if ( operand_size == address_size )
  {
    *effective_address = address.op;
    if ( offset_value != 0 )
    {
      mop_t destination;
      if ( !make_scratch_register(cdg, address_size, 0, &destination) )
        return false;
      mop_t offset;
      offset.make_number(offset_value, address_size, cdg.insn.ea, 0);
      if ( cdg.emit(m_add, &address.op, &offset, &destination) == nullptr )
        return false;
      *effective_address = destination;
    }
    return true;
  }
  mop_t wide_address;
  if ( !make_scratch_register(cdg, operand_size, 0, &wide_address) )
    return false;
  if ( cdg.emit(m_xdu, &address.op, nullptr, &wide_address) == nullptr )
    return false;
  // operand_size != address_size implies a wide database (see above):
  // fold wasmhex_memory_base (+ memarg) into the wide register — with the
  // Base of 0 only the memarg contributes.
  const uint64 addend_value = uint64(wasmhex_memory_base) + offset_value;
  if ( addend_value != 0 )
  {
    mop_t wide_sum;
    if ( !make_scratch_register(cdg, operand_size, 0, &wide_sum) )
      return false;
    mop_t base_offset;
    base_offset.make_number(addend_value, operand_size, cdg.insn.ea, 0);
    if ( cdg.emit(m_add, &wide_address, &base_offset, &wide_sum) == nullptr )
      return false;
    *effective_address = wide_sum;
    return true;
  }
  *effective_address = wide_address;
  return true;
}

bool wasmhex_filter_t::resolve_data_address(uint64 linear_offset, int size, mop_t *out)
{
  if ( linear_offset > uint64(UINT64_MAX - wasmhex_memory_base) )
    return false;
  const ea_t candidate = ea_t(wasmhex_memory_base + linear_offset);
  if ( get_segment_ea(candidate) != ea_t(wasmhex_memory_base) )
    return false;
  netnode wasm_node(wasmhex_node_name, 0, false);
  uint64 span[2] = { 0, 0 };
  if ( wasm_node == BADNODE || wasm_node.supval(0, &span, sizeof(span), 'L') != sizeof(span)
       || linear_offset < span[0] || linear_offset >= span[1] )
    return false; // no wire or BSS: numeric form stays exact
  // mop_a (address-of the gvar): the offset operand evaluates to the
  // symbol's address so the engine folds the far deref into a flat read
  // of the item (`unk_X` / `*(_DWORD *)&sym`) — semantically exact. A
  // plain mop_v here reads the symbol's CONTENTS as the offset (double
  // indirection — differentially wrong).
  mop_t gvar;
  gvar.make_gvar(candidate);
  gvar.size = int16(inf_is_64bit() ? 8 : size);
  mop_addr_t *address = new mop_addr_t(gvar, NOSIZE, NOSIZE);
  if ( address == nullptr )
    return false;
  out->t = mop_a;
  out->a = address;
  out->size = gvar.size;
  return true;
}

merror_t wasmhex_filter_t::emit_c_frame_scalar_memory(codegen_t &cdg,
                                                      const memory_instruction_t &memory,
                                                      const wasm_value_t &address,
                                                      const wasm_value_t &value,
                                                      size_t address_depth)
{
  if ( !has_c_frame || !address.is_c_frame_address )
    return reject_c_frame_marker_use(cdg, "unestablished C frame memory base");
  // Normalize sp-base markers to fp-relative - the pre-prologue
  // stack pointer sits exactly one frame away from fp
  // (direction<0: sp_before = fp + frame; direction>0: sp_before = fp - frame).
  int64 effective_offset = int64(address.c_frame_offset);
  if ( address.is_c_sp_base )
    effective_offset += c_frame_metadata.direction < 0 ? int64(c_frame_metadata.c_frame_bytes)
                                                       : -int64(c_frame_metadata.c_frame_bytes);
  effective_offset += int64(cdg.insn.Op1.value);
  // The Go incoming window [frame_size+8, +extent) maps onto the
  // args area the loader reserved (frregs=8 models the return-address
  // slot, so inargoff + off is exactly the [SP+frame_size+8+off] cell).
  // Params never alias the local frame; result slots share the window.
  if ( go_frame_mode && effective_offset >= int64(c_frame_metadata.c_frame_bytes) + 8 )
  {
    const int64 window_offset = effective_offset - int64(c_frame_metadata.c_frame_bytes) - 8;
    const int64 arg_extent = int64(cdg.mba->fullsize) - int64(cdg.mba->inargoff);
    const int64 arg_stkoff = int64(cdg.mba->inargoff) + window_offset;
    if ( window_offset >= 0 && arg_stkoff >= int64(cdg.mba->inargoff)
         && arg_stkoff + int64(memory.width) <= int64(cdg.mba->inargoff) + arg_extent )
    {
      mop_t arg_cell;
      arg_cell.make_stkvar(cdg.mba, sval_t(arg_stkoff));
      arg_cell.size = memory.width;
      const bool arg_is_float = memory.type == 0x7D || memory.type == 0x7C;
      if ( memory.store )
      {
        mop_t stored_value = value.op;
        if ( memory.width > wasm_type_size(memory.type)
             || (memory.width < wasm_type_size(memory.type)
                 && !stored_value.make_low_half(memory.width)) )
          return MERR_BADARCH;
        if ( !arg_is_float
             && !widen_materialized_address(cdg, &stored_value, int(memory.width), 2) )
          return MERR_MEM;
        return emit_move(cdg, stored_value, arg_cell, arg_is_float) ? MERR_OK : MERR_INTERR;
      }
      const int result_size = wasm_type_size(memory.type);
      if ( result_size < memory.width )
        return MERR_BADARCH;
      mop_t loaded;
      if ( !make_operand_register(cdg, memory.type, address_depth, &loaded) )
        return MERR_MEM;
      if ( result_size == memory.width )
      {
        if ( !emit_move(cdg, arg_cell, loaded, arg_is_float) )
          return MERR_INTERR;
        push(loaded, memory.type);
        return MERR_OK;
      }
      if ( cdg.emit(memory.signed_load ? m_xds : m_xdu, &arg_cell, nullptr, &loaded) == nullptr )
        return MERR_INTERR;
      push(loaded, memory.type);
      return MERR_OK;
    }
    // Outside the reserved extent: degrade to the linear fallback below.
  }
  if ( effective_offset < 0
       || effective_offset + int64(memory.width) > int64(c_frame_metadata.c_frame_bytes)
       || effective_offset + int64(memory.width) > int64(cdg.mba->frsize) )
  {
    // An out-of-frame C-frame address has no decompiler stack
    // slot. Fall back to a plain linear-memory access computed from the
    // shadow stack pointer global (sp == fp after the prologue): coarse,
    // but exactly the real wasm semantics of the access.
    mop_t effective_address;
    if ( !lower_c_frame_marker_address(cdg, address, address_depth, &effective_address) )
      return reject_c_frame_marker_use(cdg, "unlowerable C frame memory base");
    if ( inf_is_64bit() && effective_address.size == 4 )
    {
      mop_t wide_address;
      if ( !make_scratch_register(cdg, 8, 0, &wide_address) )
        return MERR_MEM;
      if ( cdg.emit(m_xdu, &effective_address, nullptr, &wide_address) == nullptr )
        return MERR_INTERR;
      if ( wasmhex_memory_base != 0 )
      {
        mop_t wide_sum;
        if ( !make_scratch_register(cdg, 8, 0, &wide_sum) )
          return MERR_MEM;
        mop_t base;
        base.make_number(wasmhex_memory_base, 8, cdg.insn.ea, 0);
        if ( cdg.emit(m_add, &wide_address, &base, &wide_sum) == nullptr )
          return MERR_INTERR;
        wide_address = wide_sum;
      }
      effective_address = wide_address;
    }
    mop_t selector;
    if ( !make_wasm_memory_selector(&selector, cdg.insn.ea, bounds_mode, false) )
      return MERR_BADARCH;
    if ( memory.store )
    {
      mop_t stored_value = value.op;
      if ( memory.width > wasm_type_size(memory.type)
           || (memory.width < wasm_type_size(memory.type)
               && !stored_value.make_low_half(memory.width)) )
        return MERR_BADARCH;
      // A materialized `&stack_cell` escape value is 4
      // bytes - the 8-byte m_stx value operand must widen once (the
      // store-value twin of the 50826 address rule).
      if ( memory.type != 0x7D && memory.type != 0x7C
           && !widen_materialized_address(cdg, &stored_value, int(memory.width), 2) )
        return MERR_MEM;
      return cdg.emit(m_stx, &stored_value, &selector, &effective_address) == nullptr ? MERR_INTERR
                                                                                      : MERR_OK;
    }
    const int result_size = wasm_type_size(memory.type);
    if ( result_size < memory.width )
      return MERR_BADARCH;
    mop_t loaded;
    if ( !(result_size == memory.width
             ? make_operand_register(cdg, memory.type, address_depth, &loaded)
             : make_scratch_register(cdg, memory.width, 1, &loaded)) )
      return MERR_MEM;
    if ( cdg.emit(m_ldx, &selector, &effective_address, &loaded) == nullptr )
      return MERR_INTERR;
    if ( result_size == memory.width )
    {
      push(loaded, memory.type);
      return MERR_OK;
    }
    mop_t result;
    if ( !make_operand_register(cdg, memory.type, address_depth, &result) )
      return MERR_MEM;
    if ( cdg.emit(memory.signed_load ? m_xds : m_xdu, &loaded, nullptr, &result) == nullptr )
      return MERR_INTERR;
    push(result, memory.type);
    return MERR_OK;
  }

  mop_t stkvar;
  stkvar.make_stkvar(cdg.mba, sval_t(effective_offset));
  stkvar.size = memory.width;
  const bool is_float = memory.type == 0x7D || memory.type == 0x7C;
  if ( memory.store )
  {
    mop_t stored_value = value.op;
    if ( memory.width > wasm_type_size(memory.type)
         || (memory.width < wasm_type_size(memory.type)
             && !stored_value.make_low_half(memory.width)) )
      return MERR_BADARCH;
    uint64 raw_const = value.constant;
    if ( !is_float && memory.width == 8
         && (value.has_constant || stored_value.is_constant(&raw_const, false)) && raw_const == 0 )
    {
      // Split a constant-zero 8-byte C-frame store into two
      // 4-byte zero stores at [effective_offset] and [effective_offset + 4]
      // so 12-byte C++ frame structs (std::vector {begin, end, cap}) do not
      // coalesce begin/end into an 8-byte lvar accessed via LODWORD/HIDWORD.
      mop_t zero_word;
      zero_word.make_number(0, 4, cdg.insn.ea, 0);
      mop_t low_stkvar;
      low_stkvar.make_stkvar(cdg.mba, sval_t(effective_offset));
      low_stkvar.size = 4;
      mop_t high_stkvar;
      high_stkvar.make_stkvar(cdg.mba, sval_t(effective_offset + 4));
      high_stkvar.size = 4;
      minsn_t *lo_mov = cdg.emit(m_mov, &zero_word, nullptr, &low_stkvar);
      minsn_t *hi_mov = cdg.emit(m_mov, &zero_word, nullptr, &high_stkvar);
      if ( lo_mov == nullptr || hi_mov == nullptr )
        return MERR_INTERR;
      lo_mov->clr_combinable();
      hi_mov->clr_combinable();
      return MERR_OK;
    }
    // A materialized frame-cell address is 4 bytes on
    // wasm32 — an 8-byte store (the Go `&stack_cell` escape word) must
    // widen it once, otherwise the move is an m_mov size mismatch (INTERR
    // 50836). The materialized value may be the &stkvar address (mop_a)
    // or the raw sp_global+k register (mop_r, an out-of-frame cell) —
    // widen by size, not by mop type.
    if ( !is_float && !widen_materialized_address(cdg, &stored_value, int(memory.width), 2) )
      return MERR_MEM;
    return emit_move(cdg, stored_value, stkvar, is_float) ? MERR_OK : MERR_INTERR;
  }

  const int result_size = wasm_type_size(memory.type);
  if ( result_size < memory.width )
    return MERR_BADARCH;
  mop_t loaded;
  if ( !make_operand_register(cdg, memory.type, address_depth, &loaded) )
    return MERR_MEM;
  if ( result_size == memory.width )
  {
    if ( !emit_move(cdg, stkvar, loaded, is_float) )
      return MERR_INTERR;
    push(loaded, memory.type);
    return MERR_OK;
  }

  if ( cdg.emit(memory.signed_load ? m_xds : m_xdu, &stkvar, nullptr, &loaded) == nullptr )
    return MERR_INTERR;
  push(loaded, memory.type);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_fixed_memory(codegen_t &cdg, const memory_instruction_t &memory,
                                             uchar memory_mode, uint64 memory_bytes)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  ea_t check_ea = BADADDR;
  const bool require_bounds_edge = bounds_mode != wasmhex_bounds_mode_t::hidden;
  if ( (memory_mode != 1 && memory_mode != 2)
       || !valid_fixed_memory_extent(memory_mode, memory_bytes)
       || (memory_mode == 1 && cdg.insn.Op1.value > UINT32_MAX)
       || (require_bounds_edge
           && (wasm_node.supval_ea(cdg.insn.ea, &check_ea, sizeof(check_ea), 'B')
                 != sizeof(check_ea)
               || check_ea == BADADDR)) )
  {
    msg("WASMHEX_F5_UNSUPPORTED: scalar access has no fixed-memory bounds edge at %a\n",
        cdg.insn.ea);
    return MERR_BADARCH;
  }

  wasm_value_t value, address;
  // Same go-frame `&stack_cell` escape as the growable path.
  if ( memory.store
       && !pop(memory.type, &value, has_c_frame && (memory.type == 0x7F || go_frame_mode)) )
    return MERR_BADARCH;
  if ( memory.store && is_c_frame_marker(value) && !materialize_c_frame_marker(cdg, &value) )
  {
    // Storing an out-of-frame C-frame address degrades to the
    // sp-based linear-memory address instead of failing the function.
    mop_t raw_address;
    if ( !lower_c_frame_marker_address(cdg, value, stack.size(), &raw_address) )
      return reject_c_frame_marker_use(cdg, "store value out of frame");
    value.op = raw_address;
  }
  const uchar address_type = memory_mode == 2 ? 0x7E : 0x7F;
  const int address_size = memory_mode == 2 ? 8 : 4;
  if ( !pop(address_type, &address, has_c_frame && address_type == 0x7F) )
    return MERR_BADARCH;
  const size_t address_depth = stack.size();
  if ( is_c_frame_marker(address) )
    return emit_c_frame_scalar_memory(cdg, memory, address, value, address_depth);
  // The proven receiver-pointer address takes the typed
  // member route (loads only; stores keep the flat address until measured).
  if ( !memory.store && address.is_go_receiver_address && go_frame_mode )
    return emit_go_receiver_member_load(cdg, memory, address, address_depth);

  mop_t effective_address;
  bool constant_address = false;
  bool statically_trapping = false;
  if ( !make_effective_memory_address(cdg, address, address_size, cdg.insn.Op1.value, memory.width,
                                      memory_bytes, &effective_address, &constant_address,
                                      &statically_trapping) )
    return MERR_MEM;
  if ( statically_trapping )
    return emit_static_memory_trap(cdg);

  mop_t selector;
  if ( !make_wasm_memory_selector(&selector, cdg.insn.ea, bounds_mode, constant_address) )
    return MERR_BADARCH;
  if ( memory.store )
  {
    mop_t stored_value = value.op;
    if ( memory.width > wasm_type_size(memory.type)
         || (memory.width < wasm_type_size(memory.type)
             && !stored_value.make_low_half(memory.width)) )
      return MERR_BADARCH;
    // A materialized `&stack_cell` escape value is 4
    // bytes - the 8-byte m_stx value operand must widen once (the
    // store-value twin of the 50826 address rule).
    if ( memory.type != 0x7D && memory.type != 0x7C
         && !widen_materialized_address(cdg, &stored_value, int(memory.width), 2) )
      return MERR_MEM;
    return cdg.emit(m_stx, &stored_value, &selector, &effective_address) == nullptr ? MERR_INTERR
                                                                                    : MERR_OK;
  }

  const int result_size = wasm_type_size(memory.type);
  if ( result_size < memory.width )
    return MERR_BADARCH;
  mop_t loaded;
  if ( !(result_size == memory.width
           ? make_operand_register(cdg, memory.type, address_depth, &loaded)
           : make_scratch_register(cdg, memory.width, 1, &loaded)) )
    return MERR_MEM;
  if ( cdg.emit(m_ldx, &selector, &effective_address, &loaded) == nullptr )
    return MERR_INTERR;
  if ( result_size == memory.width )
  {
    push(loaded, memory.type);
    return MERR_OK;
  }

  mop_t result;
  if ( !make_operand_register(cdg, memory.type, address_depth, &result) )
    return MERR_MEM;
  if ( cdg.emit(memory.signed_load ? m_xds : m_xdu, &loaded, nullptr, &result) == nullptr )
    return MERR_INTERR;
  push(result, memory.type);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_memory(codegen_t &cdg)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  uchar memory_mode = 0;
  if ( wasm_node == BADNODE
       || wasm_node.supval(0, &memory_mode, sizeof(memory_mode), 'M') != sizeof(memory_mode)
       || (memory_mode != 1 && memory_mode != 2) )
    return MERR_BADARCH;
  const size_t index = size_t(cdg.insn.itype - wasmhex_i32_load);
  if ( index >= qnumber(memory_instructions) )
    return MERR_BADARCH;
  const memory_instruction_t &memory = memory_instructions[index];
  uint64 memory_bytes = 0;
  bool growable_memory = false;
  if ( !get_wasm_memory_extent(wasm_node, memory_mode, &memory_bytes, &growable_memory) )
  {
    if ( memory_mode == 2 )
      return reject_memory64_addressing(cdg);
    msg("WASMHEX_F5_UNSUPPORTED: scalar access requires a fixed-memory proof at %a\n", cdg.insn.ea);
    return MERR_BADARCH;
  }
  if ( growable_memory && bounds_mode == wasmhex_bounds_mode_t::explicit_bounds )
    return emit_growable_memory(cdg, memory, memory_mode);
  if ( !fixed_memory_extent_fits_ida(memory_mode, memory_bytes) )
    return reject_unmapped_memory_extent(cdg);
  return emit_fixed_memory(cdg, memory, memory_mode, memory_bytes);
}

merror_t wasmhex_filter_t::emit_memory_size_grow(codegen_t &cdg, bool grow)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  uchar memory_mode = 0;
  if ( wasm_node == BADNODE
       || wasm_node.supval(0, &memory_mode, sizeof(memory_mode), 'M') != sizeof(memory_mode)
       || (memory_mode != 1 && memory_mode != 2) )
    return MERR_BADARCH;
  const uchar value_type = memory_mode == 2 ? 0x7E : 0x7F;
  if ( !grow )
  {
    uint64 memory_bytes = 0;
    if ( wasm_node.supval(0, &memory_bytes, sizeof(memory_bytes), 'm') == sizeof(memory_bytes) )
    {
      if ( !valid_fixed_memory_extent(memory_mode, memory_bytes) )
        return MERR_BADARCH;
      const uint64 pages = memory_bytes == UINT64_MAX ? uint64(1) << 48 : memory_bytes >> 16;
      const int size = value_type == 0x7E ? 8 : 4;
      mop_t result;
      if ( !make_operand_register(cdg, value_type, stack.size(), &result) )
        return MERR_MEM;
      mop_t constant;
      constant.make_number(pages, size, cdg.insn.ea, 0);
      if ( !emit_move(cdg, constant, result, false) )
        return MERR_INTERR;
      push(result, value_type);
      return MERR_OK;
    }
  }
  if ( bounds_mode == wasmhex_bounds_mode_t::hidden )
  {
    wasm_value_t delta;
    if ( grow && !pop(value_type, &delta) )
      return MERR_BADARCH;
    mop_t memory_index;
    memory_index.make_number(0, 4, cdg.insn.ea, 0);
    push(memory_index, 0x7F);
    if ( grow )
      push(delta.op, value_type);
    const uchar builtin_args[] = { 0x7F, value_type };
    return emit_call(cdg, grow ? "__builtin_wasm_memory_grow" : "__builtin_wasm_memory_size",
                     grow ? 2 : 1, value_type, false, builtin_args);
  }
  const uchar argument_types[] = { value_type };
  return emit_call(cdg, grow ? "wasm_memory_grow" : "wasm_memory_size", grow ? 1 : 0, value_type,
                   false, argument_types);
}

merror_t wasmhex_filter_t::emit_memory_bulk(codegen_t &cdg, bool copy)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  uchar memory_mode = 0;
  if ( wasm_node == BADNODE
       || wasm_node.supval(0, &memory_mode, sizeof(memory_mode), 'M') != sizeof(memory_mode)
       || (memory_mode != 1 && memory_mode != 2) )
    return MERR_BADARCH;
  const uchar address_type = memory_mode == 2 ? 0x7E : 0x7F;
  const uchar copy_arguments[] = { address_type, address_type, address_type };
  const uchar fill_arguments[] = { address_type, 0x7F, address_type };
  if ( bounds_mode == wasmhex_bounds_mode_t::hidden )
  {
    const char *builtin = copy ? "memmove" : "memset";
    uint64 memory_bytes = 0;
    bool growable = false;
    const size_t argument_count = 3;
    if ( get_wasm_memory_extent(wasm_node, memory_mode, &memory_bytes, &growable)
         && stack.size() >= argument_count )
    {
      const wasm_value_t &dest_peek = stack[stack.size() - 3];
      const wasm_value_t &middle_peek = stack[stack.size() - 2];
      const wasm_value_t &size_peek = stack[stack.size() - 1];
      const bool plain = !is_c_frame_marker(dest_peek) && !is_c_frame_marker(middle_peek)
        && !is_c_frame_marker(size_peek) && middle_peek.type == (copy ? address_type : 0x7F)
        && size_peek.type == address_type && dest_peek.type == address_type;
      if ( plain )
      {
        const int address_size = memory_mode == 2 ? 8 : 4;
        // The rebased operand size mirrors make_effective_memory_address:
        // a wide database widens wasm32 addresses to 8 bytes; everything
        // else keeps the wasm address width.
        const uchar rebased_type = inf_is_64bit() ? uchar(0x7E) : address_type;
        wasm_value_t size_value;
        wasm_value_t middle_value;
        wasm_value_t dest_value;
        if ( !pop(address_type, &size_value)
             || !pop(copy ? address_type : uchar(0x7F), &middle_value)
             || !pop(address_type, &dest_value) )
          return MERR_BADARCH;
        mop_t dest_mop;
        bool dest_constant = false;
        bool dest_trap = false;
        if ( !make_effective_memory_address(cdg, dest_value, address_size, 0, 1, memory_bytes,
                                            &dest_mop, &dest_constant, &dest_trap) )
        {
          return MERR_MEM;
        }
        if ( copy )
        {
          // A constant destination may live in the shared scratch register
          // the next transform reuses — fall back to the raw form there.
          mop_t src_mop;
          bool src_constant = false;
          bool src_trap = false;
          if ( !dest_constant )
          {
            // Park the destination in an operand register so the source
            // transform can reuse the scratch register safely.
            mop_t dest_reg;
            if ( make_operand_register(cdg, rebased_type, stack.size(), &dest_reg)
                 && cdg.emit(m_mov, &dest_mop, nullptr, &dest_reg) != nullptr
                 && make_effective_memory_address(cdg, middle_value, address_size, 0, 1,
                                                  memory_bytes, &src_mop, &src_constant,
                                                  &src_trap) )
            {
              if ( dest_trap || src_trap )
                return emit_static_memory_trap(cdg);
              const uchar rebased_arguments[] = { rebased_type, rebased_type, address_type };
              const mop_t *preset_arguments[] = { &dest_reg, &src_mop, nullptr };
              push(size_value.op, address_type);
              return emit_call(cdg, builtin, 3, 0x40, false, rebased_arguments, false, false, false,
                               false, preset_arguments);
            }
          }
          push(dest_value.op, address_type);
          push(middle_value.op, address_type);
          push(size_value.op, address_type);
          return emit_call(cdg, builtin, 3, 0x40, false, copy_arguments);
        }
        if ( dest_trap )
          return emit_static_memory_trap(cdg);
        const uchar rebased_arguments[] = { rebased_type, 0x7F, address_type };
        const mop_t *preset_arguments[] = { &dest_mop, nullptr, nullptr };
        push(middle_value.op, 0x7F);
        push(size_value.op, address_type);
        return emit_call(cdg, builtin, 3, 0x40, false, rebased_arguments, false, false, false,
                         false, preset_arguments);
      }
    }
    return emit_call(cdg, builtin, 3, 0x40, false, copy ? copy_arguments : fill_arguments);
  }
  const char *helper = copy ? memory_mode == 2 ? "wasm_memory_copy64" : "wasm_memory_copy32"
    : memory_mode == 2      ? "wasm_memory_fill64"
                            : "wasm_memory_fill32";
  return emit_call(cdg, helper, 3, 0x40, false, copy ? copy_arguments : fill_arguments);
}

merror_t wasmhex_filter_t::emit_memory_init(codegen_t &cdg)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  uchar memory_mode = 0;
  uint32 data_count = 0;
  if ( wasm_node == BADNODE
       || wasm_node.supval(0, &memory_mode, sizeof(memory_mode), 'M') != sizeof(memory_mode)
       || (memory_mode != 1 && memory_mode != 2)
       || wasm_node.supval(0, &data_count, sizeof(data_count), 'd') != sizeof(data_count)
       || cdg.insn.Op1.value > UINT32_MAX || cdg.insn.Op1.value >= data_count
       || cdg.insn.Op2.value != 0 )
    return MERR_BADARCH;
  const uchar address_type = memory_mode == 2 ? 0x7E : 0x7F;
  const uchar argument_types[] = { address_type, 0x7F, 0x7F };
  qstring helper;
  helper.sprnt("wasm_memory_init_data%u_%s", uint32(cdg.insn.Op1.value),
               memory_mode == 2 ? "i64" : "i32");
  return emit_call(cdg, helper.c_str(), 3, 0x40, false, argument_types);
}
}
