#include "filter.hpp"
namespace wasmhex
{
void ensure_libcxx_rewriter_installed();

bool wasmhex_filter_t::make_local_storage(codegen_t &cdg, uchar type, mop_t *local)
{
  ensure_libcxx_rewriter_installed();
  const int size = wasm_type_size(type);
  if ( local == nullptr || locals.empty() || (size != 4 && size != 8) )
    return false;
  const size_t local_index = locals.size() - 1;
  if ( local_index >= size_t(wasmhex_local_kreg_budget) )
  {
    const size_t c_frame_bytes = has_c_frame ? size_t(c_frame_metadata.c_frame_bytes) : 0;
    const std::int64_t raw_stkoff = wasmhex_overflow_local_stkoff(local_index, c_frame_bytes);
    if ( raw_stkoff < 0 || raw_stkoff > std::int64_t(std::numeric_limits<sval_t>::max() - size) )
      return false;
    const sval_t stkoff = sval_t(raw_stkoff);
    if ( stkoff + size > cdg.mba->frsize )
      return false;
    local->make_stkvar(cdg.mba, stkoff);
    local->size = size;
    return true;
  }
  const mreg_t reg = cdg.mba->alloc_kreg(size);
  if ( reg != mr_none )
  {
    // Insert a 4-byte guard gap after narrow kregs in C-frame
    // functions so adjacent 4-byte locals (e.g. `this` at kr00_4 and `a2` at
    // kr04_4) never sit at contiguous mreg offsets where Hex-Rays combines
    // paired moves into a single 8-byte `kr00_8` lvar.
    if ( has_c_frame && size < 8 )
      (void)cdg.mba->alloc_kreg(4);
    local->make_reg(reg, size);
    return true;
  }
  return false;
}

bool wasmhex_filter_t::make_operand_register(codegen_t &cdg, uchar type, size_t depth, mop_t *value)
{
  ensure_libcxx_rewriter_installed();
  const int size = wasm_type_size(type);
  if ( value == nullptr || (size != 1 && size != 2 && size != 4 && size != 8 && size != 16) )
    return false;
  auto bind_value = [&](mreg_t reg)
  {
    if ( size == 16 )
      value->make_reg_pair(reg, reg + 8, 8);
    else
      value->make_reg(reg, size);
  };
  const auto found = operand_register_index.find((uint64_t(depth) << 8) | uint64_t(type));
  if ( found != operand_register_index.end() )
  {
    bind_value(found->second);
    return true;
  }
  if ( operand_register_index.size() >= size_t(wasmhex_operand_kreg_budget) )
    return false;
  const mreg_t reg = cdg.mba->alloc_kreg(size);
  if ( reg == mr_none )
    return false;
  if ( has_c_frame && size < 8 )
    (void)cdg.mba->alloc_kreg(4);
  operand_register_index.emplace((uint64_t(depth) << 8) | uint64_t(type), reg);
  bind_value(reg);
  return true;
}

bool wasmhex_filter_t::make_scratch_register(codegen_t &cdg, int size, size_t index, mop_t *value,
                                             mreg_t *out_reg)
{
  if ( value == nullptr || (size != 1 && size != 2 && size != 4 && size != 8 && size != 16) )
    return false;
  const auto found
    = scratch_register_index.find((uint64_t(uint32_t(size)) << 32) | uint64_t(uint32_t(index)));
  if ( found != scratch_register_index.end() )
  {
    value->make_reg(found->second, size);
    if ( out_reg != nullptr )
      *out_reg = found->second;
    return true;
  }
  if ( scratch_register_index.size() >= size_t(wasmhex_scratch_kreg_budget) )
    return false;
  const mreg_t reg = cdg.mba->alloc_kreg(size);
  if ( reg == mr_none )
    return false;
  if ( has_c_frame && size < 8 )
    (void)cdg.mba->alloc_kreg(4);
  scratch_register_index.emplace((uint64_t(uint32_t(size)) << 32) | uint64_t(uint32_t(index)), reg);
  value->make_reg(reg, size);
  if ( out_reg != nullptr )
    *out_reg = reg;
  return true;
}

bool wasmhex_filter_t::make_boolean_register(codegen_t &cdg, mop_t *value)
{
  if ( value == nullptr )
    return false;
  if ( boolean_scratch_reg == mr_none )
    boolean_scratch_reg = cdg.mba->alloc_kreg(1);
  if ( boolean_scratch_reg == mr_none )
    return false;
  value->make_reg(boolean_scratch_reg, 1);
  return true;
}

merror_t wasmhex_filter_t::spill_value(codegen_t &cdg, wasm_value_t *value, size_t depth)
{
  if ( is_c_frame_marker(*value) || value->op.is_stkvar() )
    return MERR_OK;
  mop_t slot;
  if ( !make_operand_register(cdg, value->type, depth, &slot)
       || !emit_move(cdg, value->op, slot, value->type == 0x7D || value->type == 0x7C) )
    return MERR_INTERR;
  value->op = slot;
  return MERR_OK;
}

bool wasmhex_filter_t::alloc_local(codegen_t &cdg, size_t index, bool read_before_write)
{
  if ( index >= locals.size() )
    return false;
  wasm_local_t &local = locals[index];
  const int size = int(local.tif.get_size());
  if ( (size != 4 && size != 8) || local.storage.empty() )
    return false;
  if ( local.initialized || !read_before_write )
    return true;

  mop_t source;
  if ( index < function_type.size() )
  {
    if ( !make_regloc_mop(cdg, function_type[index].argloc, size, &source) )
      return false;
  }
  else if ( local.tif.is_floating() )
  {
    uint64 zero = 0;
    if ( !source.make_fpnum(&zero, size_t(size)) )
      return false;
  }
  else
  {
    source.make_number(0, size, cdg.insn.ea, 0);
  }

  if ( !emit_move(cdg, source, local.storage, local.tif.is_floating()) )
    return false;
  local.initialized = true;
  return true;
}
}
