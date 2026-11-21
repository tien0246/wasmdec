#include "filter.hpp"
namespace wasmhex
{
merror_t wasmhex_filter_t::emit_atomic(codegen_t &cdg)
{
  wasmhex_atomic_info_t atomic;
  if ( !wasmhex_atomic_info(cdg.insn.Op1.value, &atomic) )
    return MERR_BADARCH;
  auto invalid_operands = [&]()
  {
    msg("WASMHEX_F5_UNSUPPORTED: atomic operand stack/type mismatch at %a (subopcode=%u)\n",
        cdg.insn.ea, unsigned(cdg.insn.Op1.value));
    return MERR_BADARCH;
  };
  if ( atomic.kind == wasmhex_atomic_kind_t::fence )
  {
    const uchar no_argument_types[] = { 0 };
    return emit_call(cdg, "wasm_atomic_fence_seq_cst", 0, 0x40, false, no_argument_types);
  }

  netnode wasm_node(wasmhex_node_name, 0, false);
  uchar memory_mode = 0, memory_shared = 0;
  if ( wasm_node == BADNODE
       || wasm_node.supval(0, &memory_mode, sizeof(memory_mode), 'M') != sizeof(memory_mode)
       || (memory_mode != 1 && memory_mode != 2)
       || wasm_node.supval(0, &memory_shared, sizeof(memory_shared), 'S') != sizeof(memory_shared)
       || memory_shared > 1 )
  {
    msg("WASMHEX_F5_UNSUPPORTED: atomic memory metadata is missing or invalid at %a\n",
        cdg.insn.ea);
    return MERR_BADARCH;
  }

  const uchar address_type = memory_mode == 2 ? 0x7E : 0x7F;
  const uchar value_type = atomic.value_bits == 64 ? 0x7E : 0x7F;
  const unsigned expected_alignment = atomic.access_size == 8 ? 3
    : atomic.access_size == 4                                 ? 2
    : atomic.access_size == 2                                 ? 1
                                                              : 0;
  if ( cdg.insn.Op2.value != expected_alignment
       || (address_type == 0x7F && cdg.insn.Op3.value > UINT32_MAX) )
  {
    msg("WASMHEX_F5_UNSUPPORTED: invalid atomic memarg at %a\n", cdg.insn.ea);
    return MERR_BADARCH;
  }

  uchar input_types[4] = { address_type, value_type, value_type, address_type };
  int input_count = 0;
  uchar result_type = value_type;
  switch ( atomic.kind )
  {
    case wasmhex_atomic_kind_t::notify:
      input_types[1] = 0x7F;
      input_count = 2;
      result_type = 0x7F;
      break;
    case wasmhex_atomic_kind_t::wait32:
      input_types[1] = 0x7F;
      input_types[2] = 0x7E;
      input_count = 3;
      result_type = 0x7F;
      break;
    case wasmhex_atomic_kind_t::wait64:
      input_types[1] = 0x7E;
      input_types[2] = 0x7E;
      input_count = 3;
      result_type = 0x7F;
      break;
    case wasmhex_atomic_kind_t::load: input_count = 1; break;
    case wasmhex_atomic_kind_t::store:
    case wasmhex_atomic_kind_t::rmw: input_count = 2; break;
    case wasmhex_atomic_kind_t::cmpxchg: input_count = 3; break;
    case wasmhex_atomic_kind_t::fence: return MERR_BADARCH;
  }

  if ( !memory_shared && atomic.kind == wasmhex_atomic_kind_t::notify )
  {
    wasm_value_t count, address;
    if ( !pop(0x7F, &count) || !pop(address_type, &address) )
      return invalid_operands();
    const uchar helper_argument_types[] = { address_type, 0x7F, address_type };
    push(address.op, address_type);
    push(count.op, 0x7F);
    mop_t offset;
    offset.make_number(cdg.insn.Op3.value, address_type == 0x7E ? 8 : 4, cdg.insn.ea, 0);
    push(offset, address_type);
    const merror_t check = emit_call(cdg, "wasm_memory0_atomic_notify_check_seq_cst", 3, 0x40,
                                     false, helper_argument_types);
    if ( check != MERR_OK )
      return check;
    mop_t zero;
    zero.make_number(0, 4, cdg.insn.ea, 0);
    push(zero, 0x7F);
    return MERR_OK;
  }
  if ( !memory_shared
       && (atomic.kind == wasmhex_atomic_kind_t::wait32
           || atomic.kind == wasmhex_atomic_kind_t::wait64) )
  {
    for ( int i = input_count; i != 0; --i )
    {
      wasm_value_t ignored;
      if ( !pop(input_types[i - 1], &ignored) )
        return invalid_operands();
    }
    const uchar no_argument_types[] = { 0 };
    const char *helper = atomic.kind == wasmhex_atomic_kind_t::wait32
      ? "wasm_memory0_memory_atomic_wait32_unshared_trap_seq_cst"
      : "wasm_memory0_memory_atomic_wait64_unshared_trap_seq_cst";
    const merror_t error = emit_call(cdg, helper, 0, 0x40, false, no_argument_types, false, true);
    if ( error != MERR_OK )
      return error;
    if ( controls.empty() )
      return MERR_BADARCH;
    stack.resize(controls.back().base_stack.size());
    unreachable = true;
    return MERR_OK;
  }

  qvector<wasm_value_t> operands;
  operands.resize(input_count);
  for ( int i = input_count; i != 0; --i )
    if ( !pop(input_types[i - 1], &operands[i - 1]) )
      return invalid_operands();
  for ( int i = 0; i < input_count; ++i )
    push(operands[i].op, input_types[i]);

  mop_t offset;
  offset.make_number(cdg.insn.Op3.value, address_type == 0x7E ? 8 : 4, cdg.insn.ea, 0);
  push(offset, address_type);
  input_types[input_count] = address_type;
  const int argument_count = input_count + 1;
  const uchar helper_result = atomic.kind == wasmhex_atomic_kind_t::store ? 0x40 : result_type;

  std::string helper;
  if ( atomic.kind == wasmhex_atomic_kind_t::notify )
    helper = "wasm_memory0_memory_atomic_notify_seq_cst";
  else
  {
    helper = "wasm_memory0_";
    for ( char ch : wasmhex_atomic_mnemonic(atomic) )
      helper.push_back(ch == '.' ? '_' : ch);
    helper += "_seq_cst";
  }
  return emit_call(cdg, helper.c_str(), argument_count, helper_result, false, input_types);
}
}
