#include "filter.hpp"

#include <algorithm>
namespace wasmhex
{
merror_t wasmhex_filter_t::emit_ref_null(codegen_t &cdg)
{
  const uchar type = uchar(cdg.insn.Op1.value);
  if ( type != 0x70 && type != 0x6F && type != 0x71 && type != 0x72 )
    return MERR_BADARCH;
  mop_t value;
  value.make_number(0, 4, cdg.insn.ea, 0);
  push(value, type);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_ref_is_null(codegen_t &cdg)
{
  if ( stack.empty()
       || (stack.back().type != 0x70 && stack.back().type != 0x6F && stack.back().type != 0x71
           && stack.back().type != 0x72) )
    return MERR_BADARCH;
  return emit_eqz(cdg, stack.back().type);
}

merror_t wasmhex_filter_t::emit_ref_eq(codegen_t &cdg)
{
  if ( stack.size() < 2 || (stack.back().type != 0x71 && stack.back().type != 0x72)
       || (stack[stack.size() - 2].type != 0x71 && stack[stack.size() - 2].type != 0x72) )
    return MERR_BADARCH;
  wasm_value_t right = stack.back();
  stack.pop_back();
  wasm_value_t left = stack.back();
  stack.pop_back();
  mop_t boolean, result;
  if ( !make_boolean_register(cdg, &boolean)
       || !make_operand_register(cdg, 0x7F, stack.size(), &result) )
    return MERR_MEM;
  if ( cdg.emit(m_setz, &left.op, &right.op, &boolean) == nullptr
       || cdg.emit(m_xdu, &boolean, nullptr, &result) == nullptr )
    return MERR_INTERR;
  push(result, 0x7F);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_ref_func(codegen_t &cdg)
{
  if ( cdg.insn.Op1.value >= UINT32_MAX || !is_declared_function(uint32(cdg.insn.Op1.value)) )
    return MERR_BADARCH;
  const uint32 function_index = uint32(cdg.insn.Op1.value);
  mop_t handle;
  handle.make_number(uint64(function_index) + 1, 4, cdg.insn.ea, 0);
  push(handle, 0x70, function_index);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_ref_as_non_null(codegen_t &cdg)
{
  if ( stack.empty()
       || (stack.back().type != 0x70 && stack.back().type != 0x6F && stack.back().type != 0x71
           && stack.back().type != 0x72) )
    return MERR_BADARCH;
  const uchar type = stack.back().type;
  const uchar argument_types[] = { type };
  return emit_call(cdg,
                   type == 0x70     ? "wasm_trap_ref_as_non_null_funcref"
                     : type == 0x6F ? "wasm_trap_ref_as_non_null_externref"
                                    : "wasm_trap_ref_as_non_null_eqref",
                   1, type, false, argument_types);
}

merror_t wasmhex_filter_t::emit_ref_i31(codegen_t &cdg)
{
  wasm_value_t value;
  if ( !pop(0x7F, &value) )
    return MERR_BADARCH;
  mop_t payload, handle;
  if ( !make_scratch_register(cdg, 4, 0, &payload)
       || !make_operand_register(cdg, 0x72, stack.size(), &handle) )
    return MERR_MEM;
  mop_t payload_mask, i31_tag;
  // The size cap keeps function handles below bit 31; i31 modules reject externrefs and other eqrefs.
  payload_mask.make_number(0x7FFFFFFF, 4, cdg.insn.ea, 0);
  i31_tag.make_number(0x80000000, 4, cdg.insn.ea, 0);
  if ( cdg.emit(m_and, &value.op, &payload_mask, &payload) == nullptr
       || cdg.emit(m_or, &payload, &i31_tag, &handle) == nullptr )
    return MERR_INTERR;
  push(handle, 0x72);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_i31_get(codegen_t &cdg, bool signed_result)
{
  wasm_value_t reference;
  if ( !pop(0x72, &reference) )
    return MERR_BADARCH;
  push(reference.op, 0x72);
  const uchar argument_types[] = { 0x72 };
  merror_t error
    = emit_call(cdg, "wasm_trap_ref_as_non_null_eqref", 1, 0x72, false, argument_types);
  if ( error != MERR_OK )
    return error;
  wasm_value_t nonnull_reference;
  if ( !pop(0x72, &nonnull_reference) )
    return MERR_BADARCH;

  mop_t payload;
  if ( !(signed_result ? make_scratch_register(cdg, 4, 0, &payload)
                       : make_operand_register(cdg, 0x7F, stack.size(), &payload)) )
    return MERR_MEM;
  mop_t payload_mask;
  payload_mask.make_number(0x7FFFFFFF, 4, cdg.insn.ea, 0);
  if ( cdg.emit(m_and, &nonnull_reference.op, &payload_mask, &payload) == nullptr )
    return MERR_INTERR;
  if ( !signed_result )
  {
    push(payload, 0x7F);
    return MERR_OK;
  }

  mop_t shifted, result;
  if ( !make_scratch_register(cdg, 4, 1, &shifted)
       || !make_operand_register(cdg, 0x7F, stack.size(), &result) )
    return MERR_MEM;
  mop_t one;
  one.make_number(1, 1, cdg.insn.ea, 0);
  if ( cdg.emit(m_shl, &payload, &one, &shifted) == nullptr
       || cdg.emit(m_sar, &shifted, &one, &result) == nullptr )
    return MERR_INTERR;
  push(result, 0x7F);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_ref_call_dispatch(codegen_t &cdg)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  ea_t continuation = BADADDR;
  wasmhex_sparse_dispatch_t sparse_dispatch = {};
  uint32 case_count = 0;
  qvector<uchar> parameter_types;
  qvector<uchar> result_types;
  const uint32 type_index = uint32(cdg.insn.Op1.value);
  const bool tail_call = cdg.insn.itype == wasmhex_return_call_ref;
  if ( wasm_node == BADNODE || controls.empty()
       || (!tail_call
           && wasm_node.supval_ea(cdg.insn.ea, &continuation, sizeof(continuation), 'C')
             != sizeof(continuation))
       || wasm_node.supval_ea(cdg.insn.ea, &sparse_dispatch, sizeof(sparse_dispatch), 'J')
         != sizeof(sparse_dispatch)
       || !get_dispatch_case_count(wasm_node, cdg.insn.ea, &case_count)
       || sparse_dispatch.target_count < 2 || sparse_dispatch.target_count > 16
       || sparse_dispatch.table_size != sparse_dispatch.target_count
       || case_count != sparse_dispatch.target_count
       || !get_type_vector(wasm_node, type_index, 'A', &parameter_types)
       || !get_type_vector(wasm_node, type_index, 'B', &result_types) )
  {
    msg("WASMHEX_F5_UNSUPPORTED: reference dispatch metadata rejected at %a\n", cdg.insn.ea);
    return MERR_BADARCH;
  }
  const auto is_numeric_scalar = [](uchar type)
  {
    return type == 0x7F || type == 0x7E || type == 0x7D || type == 0x7C;
  };
  for ( uchar type : parameter_types )
    if ( !is_numeric_scalar(type) )
      return MERR_BADARCH;
  const size_t result_size = wasm_results_size(result_types);
  size_t result_words = 0;
  if ( result_size > size_t(std::numeric_limits<int>::max())
       || (result_types.size() > 1
           && !multi_result_word_count(result_types, &result_words, result_size > 16))
       || (result_types.size() == 1 && !is_numeric_scalar(result_types[0])) )
    return MERR_BADARCH;
  if ( tail_call && result_types.size() > 1 && result_size <= 16
       && (result_types.size() != 2 || result_types[0] != 0x7F
           || (result_types[1] != 0x7F && result_types[1] != 0x70 && result_types[1] != 0x6F)) )
    return MERR_BADARCH;
  if ( tail_call && result_types != function_results )
    return MERR_BADARCH;
  if ( stack.size() < controls.back().base_stack.size() + parameter_types.size() + 1 )
    return MERR_BADARCH;

  wasm_value_t reference;
  if ( !pop(0x70, &reference) || reference.op.size != 4 )
    return MERR_BADARCH;
  wasm_indirect_dispatch_t dispatch;
  dispatch.source = cdg.insn.ea;
  dispatch.continuation = continuation;
  dispatch.tail_call = tail_call;
  dispatch.reference_call = true;
  dispatch.result_types = result_types;
  dispatch.selector = reference.op;
  dispatch.arguments.resize(parameter_types.size());
  for ( size_t i = parameter_types.size(); i != 0; --i )
    if ( !pop(parameter_types[i - 1], &dispatch.arguments[i - 1],
              has_c_frame && !tail_call && parameter_types[i - 1] == 0x7F) )
      return MERR_BADARCH;
  dispatch.base_stack = stack;
  dispatch.continuation_stack = stack;
  if ( !tail_call )
  {
    for ( size_t i = 0; i < result_types.size(); ++i )
    {
      const uchar type = result_types[i];
      wasm_value_t &result = dispatch.continuation_stack.push_back();
      if ( !make_operand_register(cdg, type, dispatch.base_stack.size() + i, &result.op) )
        return MERR_MEM;
      result.type = type;
    }
  }
  if ( find_indirect_dispatch(dispatch.source) != nullptr )
    return MERR_BADARCH;
  if ( !tail_call
       && std::none_of(resume_states.begin(), resume_states.end(),
                       [&](const wasm_branch_resume_t &item)
                       { return item.target == continuation; }) )
  {
    wasm_branch_resume_t resume;
    resume.target = continuation;
    resume.stack = dispatch.continuation_stack;
    resume_states.push_back(resume);
  }
  indirect_dispatches.push_back(dispatch);
  stack = tail_call ? dispatch.base_stack : dispatch.continuation_stack;
  if ( tail_call )
    unreachable = true;
  // Same metadata-driven chain as the sparse call_indirect path
  // the 'j' case blob at the call source EA replaces the stub stream.
  qvector<uint32> dispatch_cases;
  if ( !load_dispatch_cases(cdg.insn.ea, &dispatch_cases)
       || dispatch_cases.size() / 3 != case_count )
  {
    msg("WASMHEX_F5_UNSUPPORTED: reference dispatch case blob missing or "
        "inconsistent at %a\n",
        cdg.insn.ea);
    return MERR_BADARCH;
  }
  return emit_dispatch_chain(cdg, dispatch, continuation, dispatch_cases);
}
}
