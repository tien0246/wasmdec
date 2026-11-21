#include "filter.hpp"
namespace wasmhex
{
merror_t wasmhex_filter_t::emit_v128_memory(codegen_t &cdg, bool store, int access_width,
                                            bool splat)
{
  const bool is_store = cdg.insn.itype == wasmhex_v128_store;
  const bool is_full_load = cdg.insn.itype == wasmhex_v128_load;
  const bool is_zero_load
    = cdg.insn.itype == wasmhex_v128_load32_zero || cdg.insn.itype == wasmhex_v128_load64_zero;
  const bool is_splat_load = cdg.insn.itype == wasmhex_v128_load8_splat
    || cdg.insn.itype == wasmhex_v128_load16_splat || cdg.insn.itype == wasmhex_v128_load32_splat
    || cdg.insn.itype == wasmhex_v128_load64_splat;
  const bool is_widen_load
    = cdg.insn.itype >= wasmhex_v128_load8x8_s && cdg.insn.itype <= wasmhex_v128_load32x2_u;
  const int expected_width = cdg.insn.itype == wasmhex_v128_load8_splat                         ? 1
    : cdg.insn.itype == wasmhex_v128_load16_splat                                               ? 2
    : cdg.insn.itype == wasmhex_v128_load32_zero || cdg.insn.itype == wasmhex_v128_load32_splat ? 4
    : cdg.insn.itype == wasmhex_v128_load64_zero || cdg.insn.itype == wasmhex_v128_load64_splat ? 8
    : is_widen_load                                                                             ? 8
                    : 16;
  if ( (!is_store && !is_full_load && !is_zero_load && !is_splat_load) && !is_widen_load )
  {
    msg("WASMHEX_F5_UNSUPPORTED: invalid checked native SIMD memory lowering at %a\n", cdg.insn.ea);
    return MERR_BADARCH;
  }
  if ( store != is_store || access_width != expected_width || splat != is_splat_load
       || (is_widen_load && (store || splat)) )
  {
    msg("WASMHEX_F5_UNSUPPORTED: invalid checked native SIMD memory lowering at %a\n", cdg.insn.ea);
    return MERR_BADARCH;
  }

  netnode wasm_node(wasmhex_node_name, 0, false);
  uchar memory_mode = 0;
  if ( wasm_node == BADNODE
       || wasm_node.supval(0, &memory_mode, sizeof(memory_mode), 'M') != sizeof(memory_mode)
       || (memory_mode != 1 && memory_mode != 2) )
    return MERR_BADARCH;
  uint64 memory_bytes = 0;
  ea_t check_ea = BADADDR;
  const bool require_bounds_edge = bounds_mode != wasmhex_bounds_mode_t::hidden;
  if ( !get_wasm_memory_extent(wasm_node, memory_mode, &memory_bytes)
       || (require_bounds_edge
           && (wasm_node.supval_ea(cdg.insn.ea, &check_ea, sizeof(check_ea), 'B')
                 != sizeof(check_ea)
               || check_ea == BADADDR))
       || (memory_mode == 1 && cdg.insn.Op1.value > UINT32_MAX) )
  {
    if ( memory_mode == 2 )
      return reject_memory64_addressing(cdg);
    msg(
      "WASMHEX_F5_UNSUPPORTED: SIMD memory access has no fixed-memory proof and mapped bounds check at %a\n",
      cdg.insn.ea);
    return MERR_BADARCH;
  }
  if ( !fixed_memory_extent_fits_ida(memory_mode, memory_bytes) )
    return reject_unmapped_memory_extent(cdg);

  wasm_value_t value;
  wasm_value_t address;
  if ( store && !pop(0x7B, &value) )
    return MERR_BADARCH;
  const uchar address_type = memory_mode == 2 ? 0x7E : 0x7F;
  const int address_size = memory_mode == 2 ? 8 : 4;
  if ( !pop(address_type, &address) )
    return MERR_BADARCH;

  mop_t effective_address;
  bool constant_address = false;
  bool statically_trapping = false;
  if ( !make_effective_memory_address(cdg, address, address_size, cdg.insn.Op1.value,
                                      uint64(access_width), memory_bytes, &effective_address,
                                      &constant_address, &statically_trapping) )
    return MERR_MEM;
  if ( statically_trapping )
    return emit_static_memory_trap(cdg);

  mop_t selector;
  if ( !make_wasm_memory_selector(&selector, cdg.insn.ea, bounds_mode, constant_address) )
    return MERR_BADARCH;
  if ( store )
  {
    if ( cdg.emit(m_stx, &value.op, &selector, &effective_address) == nullptr )
      return MERR_INTERR;
    return MERR_OK;
  }

  if ( access_width == 16 )
  {
    mop_t result;
    if ( !make_operand_register(cdg, 0x7B, stack.size(), &result) )
      return MERR_MEM;
    if ( cdg.emit(m_ldx, &selector, &effective_address, &result) == nullptr )
      return MERR_INTERR;
    push(result, 0x7B);
    return MERR_OK;
  }

  mop_t loaded;
  mreg_t loaded_reg = mr_none;
  if ( !make_scratch_register(cdg, access_width, 3, &loaded, &loaded_reg) )
    return MERR_MEM;
  if ( cdg.emit(m_ldx, &selector, &effective_address, &loaded) == nullptr )
    return MERR_INTERR;

  if ( is_widen_load )
  {
    const int source_width = cdg.insn.itype <= wasmhex_v128_load8x8_u ? 1
      : cdg.insn.itype <= wasmhex_v128_load16x4_u                     ? 2
                                                                      : 4;
    const int target_width = source_width * 2;
    const int lane_count = 8 / source_width;
    const int lanes_per_half = 8 / target_width;
    const bool is_signed = cdg.insn.itype == wasmhex_v128_load8x8_s
      || cdg.insn.itype == wasmhex_v128_load16x4_s || cdg.insn.itype == wasmhex_v128_load32x2_s;
    mreg_t halves[] = { mr_none, mr_none };
    mop_t accumulators[2];
    accumulators[0].make_number(0, 8, cdg.insn.ea, 0);
    accumulators[1].make_number(0, 8, cdg.insn.ea, 0);
    for ( int lane = 0; lane < lane_count; ++lane )
    {
      const int source_shift = lane * source_width * 8;
      mop_t shifted_source = loaded;
      if ( source_shift != 0 )
      {
        mop_t shifted;
        if ( !make_scratch_register(cdg, 8, 4 + size_t(lane), &shifted) )
          return MERR_MEM;
        mop_t shift_count;
        shift_count.make_number(source_shift, 1, cdg.insn.ea, 0);
        if ( cdg.emit(m_shr, &loaded, &shift_count, &shifted) == nullptr )
          return MERR_INTERR;
        shifted_source = shifted;
      }
      if ( !shifted_source.make_low_half(source_width) )
        return MERR_BADARCH;

      mop_t extended;
      if ( !make_scratch_register(cdg, target_width, 12 + size_t(lane), &extended) )
        return MERR_MEM;
      if ( cdg.emit(is_signed ? m_xds : m_xdu, &shifted_source, nullptr, &extended) == nullptr )
        return MERR_INTERR;

      mop_t lane_bits = extended;
      if ( target_width != 8 )
      {
        mop_t lane_value;
        if ( !make_scratch_register(cdg, 8, 20 + size_t(lane), &lane_value) )
          return MERR_MEM;
        if ( cdg.emit(m_xdu, &extended, nullptr, &lane_value) == nullptr )
          return MERR_INTERR;
        lane_bits = lane_value;
      }

      const int half = lane / lanes_per_half;
      const int destination_shift = (lane % lanes_per_half) * target_width * 8;
      if ( destination_shift != 0 )
      {
        mop_t positioned;
        if ( !make_scratch_register(cdg, 8, 28 + size_t(lane), &positioned) )
          return MERR_MEM;
        mop_t shift_count;
        shift_count.make_number(destination_shift, 1, cdg.insn.ea, 0);
        if ( cdg.emit(m_shl, &lane_bits, &shift_count, &positioned) == nullptr )
          return MERR_INTERR;
        lane_bits = positioned;
      }

      mop_t accumulator;
      mreg_t accumulator_reg = mr_none;
      if ( !make_scratch_register(cdg, 8, 36 + size_t(lane), &accumulator, &accumulator_reg) )
        return MERR_MEM;
      if ( cdg.emit(m_or, &accumulators[half], &lane_bits, &accumulator) == nullptr )
        return MERR_INTERR;
      accumulators[half] = accumulator;
      halves[half] = accumulator_reg;
    }
    if ( halves[0] == mr_none || halves[1] == mr_none )
      return MERR_BADARCH;
    mop_t result;
    if ( !make_operand_register(cdg, 0x7B, stack.size(), &result) )
      return MERR_MEM;
    mop_t result_low = result, result_high = result;
    if ( !result_low.make_low_half(8) || !result_high.make_high_half(8) )
      return MERR_BADARCH;
    const mop_t half0(halves[0], 8);
    const mop_t half1(halves[1], 8);
    if ( !emit_move(cdg, half0, result_low, false) || !emit_move(cdg, half1, result_high, false) )
      return MERR_INTERR;
    push(result, 0x7B);
    return MERR_OK;
  }

  mreg_t low_reg = mr_none;
  if ( access_width == 8 )
    low_reg = loaded_reg;
  else
  {
    mop_t low;
    if ( !make_scratch_register(cdg, 8, 44, &low, &low_reg) )
      return MERR_MEM;
    if ( cdg.emit(m_xdu, &loaded, nullptr, &low) == nullptr )
      return MERR_INTERR;
  }

  if ( splat )
  {
    mreg_t lane_reg = low_reg;
    size_t splat_step = 0;
    for ( int shift = access_width * 8; shift < 64; shift *= 2, ++splat_step )
    {
      mop_t shifted, repeated;
      mreg_t repeated_reg = mr_none;
      if ( !make_scratch_register(cdg, 8, 45 + splat_step * 2, &shifted)
           || !make_scratch_register(cdg, 8, 46 + splat_step * 2, &repeated, &repeated_reg) )
        return MERR_MEM;
      const mop_t lane(lane_reg, 8);
      mop_t shift_count;
      shift_count.make_number(shift, 1, cdg.insn.ea, 0);
      if ( cdg.emit(m_shl, &lane, &shift_count, &shifted) == nullptr
           || cdg.emit(m_or, &lane, &shifted, &repeated) == nullptr )
        return MERR_INTERR;
      lane_reg = repeated_reg;
    }
    mop_t result;
    if ( !make_operand_register(cdg, 0x7B, stack.size(), &result) )
      return MERR_MEM;
    mop_t result_low = result, result_high = result;
    if ( !result_low.make_low_half(8) || !result_high.make_high_half(8) )
      return MERR_BADARCH;
    const mop_t lane(lane_reg, 8);
    if ( !emit_move(cdg, lane, result_low, false) || !emit_move(cdg, lane, result_high, false) )
      return MERR_INTERR;
    push(result, 0x7B);
    return MERR_OK;
  }

  mop_t result;
  if ( !make_operand_register(cdg, 0x7B, stack.size(), &result) )
    return MERR_MEM;
  mop_t result_low = result, result_high = result;
  if ( !result_low.make_low_half(8) || !result_high.make_high_half(8) )
    return MERR_BADARCH;
  const mop_t low_op(low_reg, 8);
  mop_t zero;
  zero.make_number(0, 8, cdg.insn.ea, 0);
  if ( !emit_move(cdg, low_op, result_low, false) || !emit_move(cdg, zero, result_high, false) )
    return MERR_INTERR;
  push(result, 0x7B);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_v128_lane_memory(codegen_t &cdg)
{
  const int itype = cdg.insn.itype;
  const bool store = itype >= wasmhex_v128_store8_lane && itype <= wasmhex_v128_store64_lane;
  const int access_width = itype == wasmhex_v128_load8_lane || itype == wasmhex_v128_store8_lane ? 1
    : itype == wasmhex_v128_load16_lane || itype == wasmhex_v128_store16_lane                    ? 2
    : itype == wasmhex_v128_load32_lane || itype == wasmhex_v128_store32_lane                    ? 4
    : itype == wasmhex_v128_load64_lane || itype == wasmhex_v128_store64_lane                    ? 8
                                                                              : 0;
  const uint64 lane_index = cdg.insn.Op2.value;
  if ( access_width == 0 || lane_index >= 16 / access_width )
  {
    msg("WASMHEX_F5_UNSUPPORTED: invalid native SIMD lane memory instruction at %a\n", cdg.insn.ea);
    return MERR_BADARCH;
  }

  netnode wasm_node(wasmhex_node_name, 0, false);
  uchar memory_mode = 0;
  if ( wasm_node == BADNODE
       || wasm_node.supval(0, &memory_mode, sizeof(memory_mode), 'M') != sizeof(memory_mode)
       || (memory_mode != 1 && memory_mode != 2) )
    return MERR_BADARCH;
  uint64 memory_bytes = 0;
  ea_t check_ea = BADADDR;
  const bool require_bounds_edge = bounds_mode != wasmhex_bounds_mode_t::hidden;
  if ( !get_wasm_memory_extent(wasm_node, memory_mode, &memory_bytes)
       || (require_bounds_edge
           && (wasm_node.supval_ea(cdg.insn.ea, &check_ea, sizeof(check_ea), 'B')
                 != sizeof(check_ea)
               || check_ea == BADADDR))
       || (memory_mode == 1 && cdg.insn.Op1.value > UINT32_MAX) )
  {
    if ( memory_mode == 2 )
      return reject_memory64_addressing(cdg);
    msg("WASMHEX_F5_UNSUPPORTED: SIMD lane memory access has no private fixed-memory proof at %a\n",
        cdg.insn.ea);
    return MERR_BADARCH;
  }
  if ( !fixed_memory_extent_fits_ida(memory_mode, memory_bytes) )
    return reject_unmapped_memory_extent(cdg);

  wasm_value_t address, vector;
  const uchar address_type = memory_mode == 2 ? 0x7E : 0x7F;
  const int address_size = memory_mode == 2 ? 8 : 4;
  if ( !pop(0x7B, &vector) || !pop(address_type, &address) )
    return MERR_BADARCH;
  mop_t effective_address;
  bool constant_address = false;
  bool statically_trapping = false;
  if ( !make_effective_memory_address(cdg, address, address_size, cdg.insn.Op1.value,
                                      uint64(access_width), memory_bytes, &effective_address,
                                      &constant_address, &statically_trapping) )
    return MERR_MEM;
  if ( statically_trapping )
    return emit_static_memory_trap(cdg);

  const int lanes_per_half = 8 / access_width;
  const int lane_shift = int(lane_index % lanes_per_half) * access_width * 8;
  const bool low_half = lane_index < uint64(lanes_per_half);
  mop_t selected_half = vector.op;
  mop_t other_half = vector.op;
  if ( (low_half && (!selected_half.make_low_half(8) || !other_half.make_high_half(8)))
       || (!low_half && (!selected_half.make_high_half(8) || !other_half.make_low_half(8))) )
    return MERR_BADARCH;

  if ( store )
  {
    mop_t shifted;
    if ( !make_scratch_register(cdg, 8, 53, &shifted) )
      return MERR_MEM;
    if ( lane_shift == 0 )
    {
      if ( cdg.emit(m_mov, &selected_half, nullptr, &shifted) == nullptr )
        return MERR_INTERR;
    }
    else
    {
      mop_t shift_count;
      shift_count.make_number(lane_shift, 1, cdg.insn.ea, 0);
      if ( cdg.emit(m_shr, &selected_half, &shift_count, &shifted) == nullptr )
        return MERR_INTERR;
    }
    mop_t scalar = shifted;
    if ( access_width != 8 && !scalar.make_low_half(access_width) )
      return MERR_BADARCH;
    mop_t selector;
    if ( !make_wasm_memory_selector(&selector, cdg.insn.ea, bounds_mode, constant_address) )
      return MERR_BADARCH;
    if ( cdg.emit(m_stx, &scalar, &selector, &effective_address) == nullptr )
      return MERR_INTERR;
    return MERR_OK;
  }

  mop_t loaded;
  mreg_t loaded_reg = mr_none;
  if ( !make_scratch_register(cdg, access_width, 54, &loaded, &loaded_reg) )
    return MERR_MEM;
  mop_t selector;
  if ( !make_wasm_memory_selector(&selector, cdg.insn.ea, bounds_mode, constant_address) )
    return MERR_BADARCH;
  minsn_t *load_insn = cdg.emit(m_ldx, &selector, &effective_address, &loaded);
  if ( load_insn == nullptr )
    return MERR_INTERR;
  if ( bounds_mode == wasmhex_bounds_mode_t::hidden )
    load_insn->clr_propagatable();

  mreg_t lane_reg = loaded_reg;
  if ( access_width != 8 )
  {
    mop_t lane;
    if ( !make_scratch_register(cdg, 8, 55, &lane, &lane_reg) )
      return MERR_MEM;
    if ( cdg.emit(m_xdu, &loaded, nullptr, &lane) == nullptr )
      return MERR_INTERR;
  }
  mop_t inserted(lane_reg, 8);
  if ( lane_shift != 0 )
  {
    mop_t shifted;
    if ( !make_scratch_register(cdg, 8, 56, &shifted) )
      return MERR_MEM;
    mop_t shift_count;
    shift_count.make_number(lane_shift, 1, cdg.insn.ea, 0);
    if ( cdg.emit(m_shl, &inserted, &shift_count, &shifted) == nullptr )
      return MERR_INTERR;
    inserted = shifted;
  }

  const uint64 lane_value_mask
    = access_width == 8 ? UINT64_MAX : (uint64(1) << (access_width * 8)) - 1;
  mop_t keep_mask;
  keep_mask.make_number(~(lane_value_mask << lane_shift), 8, cdg.insn.ea, 0);
  mop_t cleared, updated, other, result;
  if ( !make_scratch_register(cdg, 8, 57, &cleared) || !make_scratch_register(cdg, 8, 58, &updated)
       || !make_scratch_register(cdg, 8, 59, &other)
       || !make_operand_register(cdg, 0x7B, stack.size(), &result) )
    return MERR_MEM;
  mop_t result_low = result, result_high = result;
  if ( !result_low.make_low_half(8) || !result_high.make_high_half(8) )
    return MERR_BADARCH;
  if ( cdg.emit(m_mov, &other_half, nullptr, &other) == nullptr
       || cdg.emit(m_and, &selected_half, &keep_mask, &cleared) == nullptr
       || cdg.emit(m_or, &cleared, &inserted, &updated) == nullptr
       || !emit_move(cdg, low_half ? updated : other, result_low, false)
       || !emit_move(cdg, low_half ? other : updated, result_high, false) )
    return MERR_INTERR;

  push(result, 0x7B);
  return MERR_OK;
}
}
