#include "module.hpp"
namespace wasmhex
{
ssize_t wasmhex_procmod_t::output_instruction(va_list va)
{
  outctx_t *ctx = va_arg(va, outctx_t *);
  if ( ctx->insn.itype >= wasmhex_last )
    return 0;
  if ( ctx->insn.itype == wasmhex_atomic )
  {
    wasmhex_atomic_info_t atomic;
    if ( !wasmhex_atomic_info(ctx->insn.Op1.value, &atomic) )
      return 0;
    const std::string mnemonic = wasmhex_atomic_mnemonic(atomic);
    ctx->out_custom_mnem(mnemonic.c_str());
    if ( atomic.kind != wasmhex_atomic_kind_t::fence )
    {
      ctx->out_char(' ');
      if ( !ctx->out_one_operand(2) )
        return 0;
    }
    ctx->flush_outbuf();
    return 1;
  }
  ctx->out_custom_mnem(instructions[ctx->insn.itype].name);
  for ( int i = 0; i < UA_MAXOP && ctx->insn.ops[i].type != o_void; ++i )
  {
    if ( i != 0 )
    {
      ctx->out_symbol(',');
      ctx->out_char(' ');
    }
    if ( !ctx->out_one_operand(i) )
      return 0;
  }
  ctx->flush_outbuf();
  return 1;
}

ssize_t wasmhex_procmod_t::output_operand(va_list va)
{
  outctx_t *ctx = va_arg(va, outctx_t *);
  const op_t *op = va_arg(va, const op_t *);
  if ( op->type != o_imm )
    return -1;
  int flags = OOF_NUMBER;
  if ( ctx->insn.itype == wasmhex_i32_const || ctx->insn.itype == wasmhex_i64_const )
    flags |= OOF_SIGNED;
  ctx->out_value(*op, flags);
  return 1;
}
}
