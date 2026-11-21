#include "filter.hpp"

#include <algorithm>
namespace wasmhex
{
merror_t wasmhex_filter_t::emit_catch(codegen_t &cdg)
{
  const bool has_control = controls.size() > 1;
  const bool has_payload = has_control && controls.back().catch_payload_type != 0x40;
  const size_t expected_stack_size
    = has_control ? controls.back().base_stack.size() + size_t(has_payload) : 0;
  if ( controls.size() <= 1 || controls.back().kind != 0x06 || controls.back().in_catch
       || controls.back().catch_handler != cdg.insn.ea
       || !stack_has_prefix(controls.back().base_stack) || stack.size() < expected_stack_size
       || (has_payload && stack.back().type != controls.back().catch_payload_type) )
    return MERR_BADARCH;
  if ( stack.size() > expected_stack_size )
  {
    wasm_value_t payload;
    if ( has_payload )
      payload = stack.back();
    stack.resize(controls.back().base_stack.size());
    if ( has_payload )
      stack.push_back(payload);
  }
  controls.back().in_catch = true;
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_throw(codegen_t &cdg)
{
  auto fail = [&]()
  {
    msg("WASMHEX_F5_UNSUPPORTED: tagged throw has no validated in-function catch edge at %a\n",
        cdg.insn.ea);
    return MERR_BADARCH;
  };
  ea_t catch_ea = BADADDR;
  if ( cdg.insn.Op1.value > UINT32_MAX || !get_ea_value(cdg.insn.ea, 'Y', &catch_ea)
       || catch_ea == BADADDR )
    return fail();
  wasm_control_frame_t *matching_try = nullptr;
  for ( size_t i = controls.size(); i > 1; --i )
  {
    wasm_control_frame_t &candidate = controls[i - 1];
    if ( candidate.kind == 0x06 && !candidate.in_catch
         && candidate.catch_tag_index == uint32(cdg.insn.Op1.value)
         && candidate.catch_handler == catch_ea )
    {
      matching_try = &candidate;
      break;
    }
  }
  if ( matching_try == nullptr )
    return fail();

  wasm_control_frame_t &frame = *matching_try;
  wasm_value_t payload;
  const bool has_payload = frame.catch_payload_type != 0x40;
  const uchar payload_type = frame.catch_payload_type;
  if ( (frame.catch_payload_type != 0x40 && frame.catch_payload_type != 0x7F
        && frame.catch_payload_type != 0x7E && frame.catch_payload_type != 0x7D
        && frame.catch_payload_type != 0x7C)
       || (has_payload && !pop(payload_type, &payload)) || !stack_has_prefix(frame.base_stack)
       || stack.size() < frame.base_stack.size() )
    return fail();

  if ( std::any_of(resume_states.begin(), resume_states.end(),
                   [catch_ea](const wasm_branch_resume_t &resume)
                   { return resume.target == catch_ea; }) )
    return fail();
  wasm_branch_resume_t &resume = resume_states.push_back();
  resume.target = catch_ea;
  resume.stack = frame.base_stack;
  if ( has_payload )
    resume.stack.push_back(payload);

  mop_t target;
  target.make_gvar(catch_ea);
  if ( cdg.emit(m_goto, &target, nullptr, nullptr) == nullptr )
    return MERR_INTERR;
  stack = frame.base_stack;
  unreachable = true;
  return MERR_OK;
}
}
