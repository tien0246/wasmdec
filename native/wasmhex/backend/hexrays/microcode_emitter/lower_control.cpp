#include <unistd.h>
#include <algorithm>
#include <cstring>
#include "filter.hpp"

#include "../../../ida/loader/options.hpp"
#include "../../../backend/idb/metadata_schema/metadata.hpp"
namespace wasmhex
{
void wasmhex_filter_t::prune_closed_controls(ea_t ea)
{
  while ( controls.size() > 1 && controls.back().end < ea )
    controls.pop_back();
}

bool wasmhex_filter_t::read_control_types(ea_t ea, uchar tag, qvector<uchar> *types,
                                          size_t max_types)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  if ( wasm_node == BADNODE || !get_type_vector(wasm_node, ea, tag, types)
       || (max_types != 0 && types->size() > max_types) )
    return false;
  for ( uchar type : *types )
    if ( wasm_type_size(type) == 0 )
      return false;
  return true;
}

void wasmhex_filter_t::add_branch_resume(ea_t source, ea_t target,
                                         const wasm_control_frame_t &frame,
                                         qvector<wasm_branch_resume_t> *resumes)
{
  if ( target <= source )
    return;
  if ( std::any_of(resumes->begin(), resumes->end(), [target](const wasm_branch_resume_t &resume)
                   { return resume.target == target; }) )
    return;
  wasm_branch_resume_t &resume = resumes->push_back();
  resume.target = target;
  resume.stack = frame.base_stack;
  for ( const wasm_value_t &value : frame.label_values )
    resume.stack.push_back(value);
}

merror_t wasmhex_filter_t::emit_control_start(codegen_t &cdg, uchar kind)
{
  wasm_control_frame_t frame;
  qvector<uchar> parameter_types;
  ea_t catch_payload_type = 0;
  frame.kind = kind;
  frame.start = cdg.insn.ea;
  frame.entered_reachable = !unreachable;
  if ( !get_ea_value(frame.start, 'X', &frame.end)
       || !read_control_types(frame.start, 'U', &parameter_types, 0)
       || !load_control_types(DatabaseEA{ uint64(frame.start) }, &frame.result_types)
       || !read_control_types(frame.start, 'V', &frame.label_types, 0)
       || (kind == 0x03 ? !frame.label_types.empty() : frame.label_types != frame.result_types) )
    return MERR_BADARCH;
  if ( kind == 0x06 )
  {
    netnode wasm_node(wasmhex_node_name, 0, false);
    if ( !parameter_types.empty() || !frame.result_types.empty() || wasm_node == BADNODE
         || !get_ea_value(frame.start, 'H', &frame.catch_handler)
         || !get_ea_value(frame.start, 'N', &catch_payload_type)
         || wasm_node.supval_ea(frame.start, &frame.catch_tag_index, sizeof(frame.catch_tag_index),
                                'O')
           != sizeof(frame.catch_tag_index)
         || (catch_payload_type != 0x40 && catch_payload_type != 0x7F && catch_payload_type != 0x7E
             && catch_payload_type != 0x7D && catch_payload_type != 0x7C) )
      return MERR_BADARCH;
    frame.catch_payload_type = uchar(catch_payload_type);
  }
  if ( !unreachable )
    for ( size_t i = 0; i < stack.size(); ++i )
      if ( spill_value(cdg, &stack[i], i) != MERR_OK )
        return MERR_INTERR;
  if ( !unreachable )
  {
    if ( stack.size() < parameter_types.size() )
      return MERR_BADARCH;
    frame.parameter_values.resize(parameter_types.size());
    for ( size_t i = parameter_types.size(); i != 0; --i )
    {
      const bool allow_marker = has_c_frame && parameter_types[i - 1] == 0x7F;
      if ( !pop(parameter_types[i - 1], &frame.parameter_values[i - 1], allow_marker) )
        return MERR_BADARCH;
      // Block/loop parameters may be address values; materialize.
      if ( is_c_frame_marker(frame.parameter_values[i - 1])
           && !materialize_c_frame_marker(cdg, &frame.parameter_values[i - 1]) )
        return reject_c_frame_marker_use(cdg, "block parameter");
    }
  }
  frame.base_stack = stack;
  for ( const wasm_value_t &value : frame.parameter_values )
    stack.push_back(value);
  for ( size_t i = 0; i < frame.result_types.size(); ++i )
  {
    const uchar type = frame.result_types[i];
    wasm_value_t &value = frame.result_values.push_back();
    value.type = type;
    if ( !make_operand_register(cdg, type, frame.base_stack.size() + i, &value.op) )
      return MERR_MEM;
  }
  if ( kind != 0x03 )
    frame.label_values = frame.result_values;
  else
  {
    for ( size_t i = 0; i < frame.parameter_values.size(); ++i )
    {
      wasm_value_t &parameter = frame.parameter_values[i];
      wasm_value_t &label = frame.label_values.push_back();
      label.type = parameter.type;
      if ( !make_operand_register(cdg, label.type, frame.base_stack.size() + i, &label.op)
           || !emit_move(cdg, parameter.op, label.op, label.type == 0x7D || label.type == 0x7C) )
        return MERR_INTERR;
      parameter.op = label.op;
    }
    stack.resize(frame.base_stack.size());
    for ( const wasm_value_t &parameter : frame.parameter_values )
      stack.push_back(parameter);
  }
  controls.push_back(frame);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_if(codegen_t &cdg)
{
  if ( unreachable )
    return emit_control_start(cdg, 0x04);
  wasm_value_t condition;
  qvector<ea_t> targets;
  if ( !pop(0x7F, &condition) || !get_ea_list(cdg.insn.ea, 'C', &targets) || targets.size() != 1 )
    return MERR_BADARCH;
  const merror_t error = emit_control_start(cdg, 0x04);
  if ( error != MERR_OK )
    return error;
  wasm_control_frame_t &frame = controls.back();
  ea_t false_target = targets[0];
  if ( false_target < frame.end )
  {
    insn_t else_insn;
    if ( decode_insn(&else_insn, false_target) <= 0 || else_insn.itype != wasmhex_else )
      return MERR_BADARCH;
    false_target += else_insn.size;
    wasm_branch_resume_t else_resume;
    else_resume.target = false_target;
    else_resume.stack = frame.base_stack;
    for ( const wasm_value_t &parameter : frame.parameter_values )
      else_resume.stack.push_back(parameter);
    if ( std::none_of(resume_states.begin(), resume_states.end(),
                      [false_target](const wasm_branch_resume_t &resume)
                      { return resume.target == false_target; }) )
      resume_states.push_back(else_resume);
  }
  mop_t zero, target;
  zero.make_number(0, 4, cdg.insn.ea, 0);
  target.make_gvar(false_target);
  if ( cdg.emit(m_jz, &condition.op, &zero, &target) == nullptr )
    return MERR_INTERR;
  if ( frame.end < BADADDR && targets[0] == frame.end + 1 )
  {
    // The no-else false edge reaches the successor; IDA skips the empty end opcode.
    add_branch_resume(cdg.insn.ea, targets[0], frame, &resume_states);
  }
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_else(codegen_t &cdg)
{
  qvector<ea_t> targets;
  if ( controls.size() <= 1 || controls.back().kind != 0x04 || controls.back().in_else )
    return MERR_BADARCH;
  wasm_control_frame_t &frame = controls.back();
  if ( unreachable )
  {
    frame.in_else = true;
    stack = frame.base_stack;
    for ( const wasm_value_t &parameter : frame.parameter_values )
      stack.push_back(parameter);
    if ( frame.entered_reachable )
      unreachable = false;
    return MERR_OK;
  }
  if ( !get_ea_list(cdg.insn.ea, 'C', &targets) || targets.size() != 1
       || stack.size() != frame.base_stack.size() + frame.result_types.size() )
    return MERR_BADARCH;
  for ( size_t i = 0; i < frame.result_types.size(); ++i )
  {
    wasm_value_t &value = stack[frame.base_stack.size() + i];
    if ( is_c_frame_marker(value) && !materialize_c_frame_marker(cdg, &value) )
      return reject_c_frame_marker_use(cdg, "block result");
    if ( value.type != frame.result_values[i].type
         || !emit_move(cdg, value.op, frame.result_values[i].op,
                       value.type == 0x7D || value.type == 0x7C) )
      return MERR_BADARCH;
  }
  frame.in_else = true;
  stack = frame.base_stack;
  for ( const wasm_value_t &parameter : frame.parameter_values )
    stack.push_back(parameter);
  mop_t target;
  target.make_gvar(targets[0]);
  if ( cdg.emit(m_goto, &target, nullptr, nullptr) == nullptr )
    return MERR_INTERR;
  add_branch_resume(cdg.insn.ea, targets[0], frame, &resume_states);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_end(codegen_t &cdg)
{
  if ( is_function_end(cdg.insn.ea) )
  {
    if ( controls.size() != 1 )
      msg("WASMHEX_F5_DIAG: function-end with %zu open frames at %a\n", controls.size(),
          cdg.insn.ea);
    const merror_t result
      = controls.size() == 1 ? (unreachable ? MERR_OK : emit_return(cdg)) : MERR_BADARCH;
    function_end_emitted = true;
    return result;
  }
  if ( controls.size() <= 1 )
  {
    msg("WASMHEX_F5_DIAG: block-end without frame at %a\n", cdg.insn.ea);
    return MERR_BADARCH;
  }
  wasm_control_frame_t &frame = controls.back();
  if ( frame.end != cdg.insn.ea )
  {
    msg("WASMHEX_F5_DIAG: block-end frame mismatch (frame end %a) at %a\n", frame.end, cdg.insn.ea);
    return MERR_BADARCH;
  }
  if ( unreachable )
  {
    controls.pop_back();
    return MERR_OK;
  }
  // A REACHABLE block-end with surplus stack can never
  // happen in valid wasm (the validator rejects it) — it is either a
  // filter dataflow bug or an invalid module. Fail closed with a counted
  // message; the polymorphic-dead case is already handled above by the
  // `unreachable` early-out (stack-polymorphic, never trimmed).
  if ( stack.size() > frame.base_stack.size() + frame.result_types.size() )
  {
    msg("WASMHEX_TRIM: surplus stack at reachable block-end at %a "
        "(surplus=%u, results=%u) — failing closed\n",
        cdg.insn.ea, uint32(stack.size() - frame.base_stack.size() - frame.result_types.size()),
        uint32(frame.result_types.size()));
    return MERR_BADARCH;
  }
  if ( stack.size() != frame.base_stack.size() + frame.result_types.size() )
  {
    msg("WASMHEX_F5_DIAG: block-end stack arity %zu != %zu at %a\n", stack.size(),
        frame.base_stack.size() + frame.result_types.size(), cdg.insn.ea);
    return MERR_BADARCH;
  }
  for ( size_t i = 0; i < frame.result_types.size(); ++i )
  {
    wasm_value_t &value = stack[frame.base_stack.size() + i];
    if ( is_c_frame_marker(value) && !materialize_c_frame_marker(cdg, &value) )
      return reject_c_frame_marker_use(cdg, "block result");
    if ( value.type != frame.result_values[i].type
         || !emit_move(cdg, value.op, frame.result_values[i].op,
                       value.type == 0x7D || value.type == 0x7C) )
      return MERR_BADARCH;
  }
  stack = frame.base_stack;
  for ( const wasm_value_t &value : frame.result_values )
    stack.push_back(value);
  controls.pop_back();
  return MERR_OK;
}

bool wasmhex_filter_t::resolve_branch_resume_target(ea_t branch_target, bool has_branch_values,
                                                    ea_t *resume_target) const
{
  if ( resume_target == nullptr )
    return false;
  *resume_target = branch_target;
  if ( !has_branch_values )
    return true;
  insn_t stub_insn;
  if ( decode_insn(&stub_insn, branch_target) <= 0 )
    return false;
  if ( stub_insn.itype == wasmhex_branch_edge )
    return get_ea_value(branch_target, 'C', resume_target);
  return true;
}

merror_t wasmhex_filter_t::emit_br_on_reference(codegen_t &cdg, bool branch_on_null)
{
  if ( unreachable )
    return MERR_OK;
  const uint64 depth = cdg.insn.Op1.value;
  qvector<uchar> branch_types;
  qvector<ea_t> targets;
  if ( depth >= controls.size()
       || !load_control_types(DatabaseEA{ uint64(cdg.insn.ea) }, &branch_types)
       || !get_ea_list(cdg.insn.ea, 'C', &targets) || targets.size() != 1 || stack.empty() )
    return MERR_BADARCH;
  wasm_value_t reference = stack.back();
  if ( reference.type != 0x70 && reference.type != 0x6F && reference.type != 0x71
       && reference.type != 0x72 )
    return MERR_BADARCH;
  stack.pop_back();

  wasm_control_frame_t &target_frame = controls[controls.size() - 1 - size_t(depth)];
  if ( branch_types != target_frame.label_types || !stack_has_prefix(target_frame.base_stack) )
    return MERR_BADARCH;
  if ( !branch_on_null && (branch_types.empty() || branch_types.back() != reference.type) )
    return MERR_BADARCH;
  const size_t payload_count = branch_types.size();
  const size_t prefix_count = branch_on_null ? payload_count : payload_count - 1;
  if ( stack.size() < target_frame.base_stack.size() + prefix_count )
    return MERR_BADARCH;
  const size_t payload_start = stack.size() - prefix_count;
  for ( size_t i = 0; i < prefix_count; ++i )
  {
    if ( stack[payload_start + i].type != branch_types[i] )
      return MERR_BADARCH;
    if ( is_c_frame_marker(stack[payload_start + i]) )
    {
      // Materialize address payloads like emit_br does.
      if ( !materialize_c_frame_marker(cdg, &stack[payload_start + i]) )
        return reject_c_frame_marker_use(cdg, "br_on_reference payload");
    }
  }

  ea_t resume_target = targets[0];
  if ( !resolve_branch_resume_target(targets[0], payload_count != 0, &resume_target) )
    return MERR_BADARCH;

  if ( payload_count != 0 )
  {
    if ( target_frame.label_values.size() != payload_count )
      return MERR_BADARCH;
    wasm_branch_edge_t edge;
    edge.source = cdg.insn.ea;
    for ( size_t i = 0; i < payload_count; ++i )
    {
      const wasm_value_t &value = i < prefix_count ? stack[payload_start + i] : reference;
      wasm_branch_value_edge_t &copy = edge.values.push_back();
      if ( !make_operand_register(cdg, value.type, payload_start + i, &copy.spill)
           || !emit_move(cdg, value.op, copy.spill, value.type == 0x7D || value.type == 0x7C) )
        return MERR_INTERR;
      copy.merge = target_frame.label_values[i].op;
      copy.type = value.type;
      copy.floating = value.type == 0x7D || value.type == 0x7C;
    }
    // Merge moves run right before the conditional jump (the retired
    // 0xF2 stub used to own them) — same reasoning as emit_br.
    for ( const wasm_branch_value_edge_t &copy : edge.values )
      if ( !emit_move(cdg, copy.spill, copy.merge, copy.floating) )
        return MERR_INTERR;
  }

  mop_t zero, target;
  zero.make_number(0, 4, cdg.insn.ea, 0);
  target.make_gvar(targets[0]);
  if ( cdg.emit(branch_on_null ? m_jz : m_jnz, &reference.op, &zero, &target) == nullptr )
    return MERR_INTERR;
  add_branch_resume(cdg.insn.ea, resume_target, target_frame, &resume_states);
  if ( branch_on_null )
    stack.push_back(reference);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_br_table(codegen_t &cdg)
{
  // Do not skip - a returning jtbl stub set `unreachable`, but the
  // br_table dispatch chain still has to be generated (the stubs only
  // pre-spilled the payload; the selector was parked in
  // branch_table_selector by emit_branch_edge).
  wasm_value_t selector;
  qvector<ea_t> targets;
  qvector<uint32> depths;
  const bool packed_metadata = get_branch_table_metadata(cdg.insn.ea, &targets, &depths);
  const bool selector_parked = unreachable && !branch_table_selector.empty();
  if ( !selector_parked )
  {
    if ( !pop(0x7F, &selector) )
      return MERR_BADARCH;
  }
  if ( !packed_metadata
       && (!get_ea_list(cdg.insn.ea, 'C', &targets) || !get_u32_list(cdg.insn.ea, 'D', &depths)
           || targets.empty() || targets.size() != depths.size()) )
  {
    return MERR_BADARCH;
  }
  // The legacy chain-mode 'S'-dispatcher detector is retired
  // no EA-keyed 'S' metadata is written any more and br_table lowers
  // through the jtbl two-pass unconditionally.
  return emit_br_table_jtbl(cdg, targets, depths, selector);
}

// emit_branch_edge / emit_branch_goto (the 0xF2/0xFF stub
// lowerings) are retired — no stub stream enters .text and the
// filter fails closed with a reason if a stale database still
// carries those bytes.

// PERF: lower a large br_table as ONE m_jtbl switch. m_jtbl
// ("jtbl l, r=mcases") with grouped case values and unique block targets
// satisfies the mba verifier exactly (BLT_NWAY block whose succset equals
// mcases.targets, bidirectional pred/succ wiring), and the engine
// optimizer sees a single instruction instead of a thousands-block
// compare chain. Layout:
//   [head: selector spill] --goto--> [bounds: m_ja sel, case_count ->
//   default] --> [switch: m_jtbl cases 0..case_count-1] with one landing
//   pad per distinct unresolved body (fake block, single goto to the body
//   EA); bodies that already own a serial are referenced directly.
merror_t wasmhex_filter_t::emit_br_table_m_jtbl(codegen_t &cdg, const qvector<ea_t> &targets,
                                                const qvector<bool> &by_serial,
                                                const qvector<int> &target_serials,
                                                const qvector<mop_t> &direct_targets,
                                                mblock_t *head)
{
  const size_t case_count = targets.size() - 1; // last entry = default arm
  if ( head == nullptr || case_count == 0 )
    return MERR_INTERR;
  // resolve every arm to a serial: an existing block, or a landing pad
  // (appended after the bounds+switch blocks, in first-use order)
  std::vector<int> arm_serial(targets.size(), -1);
  std::map<ea_t, int> pad_for_body;
  std::vector<ea_t> pad_order;
  for ( size_t i = 0; i < targets.size(); ++i )
  {
    if ( by_serial[i] )
    {
      arm_serial[i] = target_serials[i];
      continue;
    }
    auto found = pad_for_body.find(targets[i]);
    if ( found != pad_for_body.end() )
    {
      arm_serial[i] = found->second;
      continue;
    }
    const int pad_id = -2 - int(pad_order.size());
    arm_serial[i] = pad_id;
    pad_for_body[targets[i]] = pad_id;
    pad_order.push_back(targets[i]);
  }
  const int bounds_serial = cdg.mba->qty;
  const int switch_serial = bounds_serial + 1;
  const int pad_base = bounds_serial + 2;
  for ( auto &kv : pad_for_body )
    kv.second = pad_base + (-kv.second - 2);
  for ( size_t i = 0; i < targets.size(); ++i )
    if ( arm_serial[i] < -1 )
      arm_serial[i] = pad_for_body[targets[i]];

  // group the case values per target serial — one mcases entry per
  // distinct landing, its value group holding every selector value
  // (sparse Go tables fold their repeated default arms into one group)
  mcases_t *cases = new mcases_t;
  std::map<int, size_t> entry_of_serial;
  for ( size_t i = 0; i < case_count; ++i )
  {
    auto found = entry_of_serial.find(arm_serial[i]);
    if ( found == entry_of_serial.end() )
    {
      entry_of_serial[arm_serial[i]] = cases->size();
      cases->resize(cases->size() + 1);
      cases->targets.back() = arm_serial[i];
      found = entry_of_serial.find(arm_serial[i]);
    }
    cases->values[found->second].push_back(sval_t(i));
  }
  if ( cases->empty() )
    return MERR_INTERR;
  // Bounds sends sel > case_count to the default; sel == case_count would
  // otherwise reach the jtbl as an UNMATCHED value (the engine renders
  // that as abort()). Give the default arm case_count as an explicit case
  // value: every selector in
  // [0, case_count] now lands somewhere defined, and the ja bound (rather
  // than jae) keeps the engine's switch-range analysis that collapses
  // yyjson_read_opts from 307s to 129s.
  {
    auto found = entry_of_serial.find(arm_serial.back());
    if ( found == entry_of_serial.end() )
    {
      entry_of_serial[arm_serial.back()] = cases->size();
      cases->resize(cases->size() + 1);
      cases->targets.back() = arm_serial.back();
      found = entry_of_serial.find(arm_serial.back());
    }
    cases->values[found->second].push_back(sval_t(case_count));
  }
  const int default_serial = arm_serial.back();
  const mop_t selector_mop(branch_table_selector);

  // bounds block: unsigned-above check sends out-of-range selectors to
  // the default arm (its jcnd-family outs are {fallthrough, target})
  mblock_t *bounds = cdg.mba->insert_block(cdg.mba->qty);
  if ( bounds == nullptr )
    return MERR_MEM;
  bounds->type = BLT_2WAY;
  bounds->flags |= MBL_FAKE;
  bounds->start = cdg.insn.ea;
  bounds->end = cdg.insn.ea;
  minsn_t *ja = new minsn_t(4);
  ja->opcode = m_ja; // wasm default arm: selector >= case_count <=> sel > case_count-1
  ja->ea = cdg.insn.ea;
  ja->l = selector_mop;
  ja->r.make_number(case_count, 4, cdg.insn.ea, 0);
  ja->d.make_blkref(default_serial);
  bounds->insert_into_block(ja, nullptr);

  // switch block: the single m_jtbl with the case table (wiring happens
  // after the landing pads exist — get_mblock on a future serial would
  // dereference garbage)
  mblock_t *swblk = cdg.mba->insert_block(cdg.mba->qty);
  if ( swblk == nullptr )
    return MERR_MEM;
  swblk->type = BLT_NWAY;
  swblk->flags |= MBL_FAKE;
  swblk->start = cdg.insn.ea;
  swblk->end = cdg.insn.ea;
  minsn_t *jtbl = new minsn_t(4);
  jtbl->opcode = m_jtbl;
  jtbl->ea = cdg.insn.ea;
  jtbl->l = selector_mop;
  jtbl->r._make_cases(cases);
  swblk->insert_into_block(jtbl, nullptr);
  swblk->succset = cases->targets; // exact copy — the verifier compares them

  // landing pads: one fake block with a single goto to the body EA
  for ( size_t p = 0; p < pad_order.size(); ++p )
  {
    mblock_t *pad = cdg.mba->insert_block(cdg.mba->qty);
    if ( pad == nullptr )
      return MERR_MEM;
    pad->flags |= MBL_FAKE;
    pad->start = cdg.insn.ea;
    pad->end = cdg.insn.ea;
    minsn_t *pad_goto = new minsn_t(4);
    pad_goto->opcode = m_goto;
    pad_goto->ea = cdg.insn.ea;
    pad_goto->l.make_gvar(pad_order[p]);
    pad->insert_into_block(pad_goto, nullptr);
  }

  // wire everything now that every block exists
  bounds->succset.add_unique(switch_serial);
  bounds->succset.add_unique(default_serial);
  bounds->predset.add_unique(head->serial);
  cdg.mba->get_mblock(default_serial)->predset.add_unique(bounds->serial);
  swblk->predset.add_unique(bounds->serial);
  for ( int serial : cases->targets )
    cdg.mba->get_mblock(serial)->predset.add_unique(swblk->serial);

  // bridge the codegen block into the bounds check THROUGH cdg, then wire
  // head <-> bounds both ways (same pattern as the chain entry bridge)
  mop_t bounds_entry;
  bounds_entry.make_blkref(bounds_serial);
  if ( cdg.emit(m_goto, &bounds_entry, nullptr, nullptr) == nullptr )
    return MERR_INTERR;
  head->succset.add_unique(bounds_serial);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_br_table_jtbl(codegen_t &cdg, const qvector<ea_t> &targets,
                                              const qvector<uint32> &depths,
                                              const wasm_value_t &selector)
{
  // The br_table lowering is a single m_jtbl switch (see
  // emit_br_table_m_jtbl). 1. park the selector in a kreg and spill it
  // via emit_move, 2. clear the abstract stack and end the linear path,
  // 3. queue a resume state for EVERY target (case bodies decode after
  // the br_table and rebuild their stack from the target depth's frame),
  // 4. resolve every target to an existing block serial, 5. hand the
  // table to the switch emitter.
  if ( unreachable )
  {
    // A returning stub cleared the abstract stack; the selector was
    // parked in branch_table_selector by emit_branch_edge.
    if ( branch_table_selector.empty() )
      return MERR_BADARCH;
    unreachable = false;
  }
  else
  {
    if ( branch_table_selector.empty() )
    {
      if ( !make_operand_register(cdg, 0x7F, stack.size(), &branch_table_selector) )
        return MERR_MEM;
    }
    if ( !emit_move(cdg, selector.op, branch_table_selector, false) )
      return MERR_INTERR;
  }
  stack.clear();
  unreachable = true;
  for ( size_t i = 0; i < depths.size(); ++i )
  {
    if ( depths[i] >= controls.size() )
      return MERR_BADARCH; // defensive; the reference indexes directly
    const wasm_control_frame_t &frame = controls[controls.size() - 1 - depths[i]];
    add_branch_resume(cdg.insn.ea, targets[i], frame, &resume_states);
  }
  // With the stub region gone, bodies placed AFTER the br_table do
  // not own a block yet (their instructions decode later). Such targets
  // are referenced directly by EA (mop gvar) — the codegen opens the
  // block when it reaches the address; bodies placed before the br_table
  // keep the fast serial path.
  qvector<int> target_serials;
  target_serials.resize(targets.size());
  qvector<mop_t> direct_targets;
  direct_targets.resize(targets.size());
  qvector<bool> by_serial;
  by_serial.resize(targets.size(), false);
  for ( size_t j = 0; j < targets.size(); ++j )
  {
    target_serials[j] = -1;
    for ( int k = 0; k < cdg.mba->qty; ++k )
      if ( cdg.mba->get_mblock(k)->start == targets[j] )
      {
        target_serials[j] = k;
        by_serial[j] = true;
        break;
      }
    if ( !by_serial[j] )
      direct_targets[j].make_gvar(targets[j]);
  }
  // 'C'/'c' carry the case BODY addresses directly (the stub region
  // is gone). Bodies run after the br_table with `unreachable` set by the
  // dispatch, so each body needs a resume state or the apply() gate
  // swallows its instructions (empty body blocks -> GLBOPT collapses the
  // jz chain into one jbe -> JUMPOUT).
  for ( size_t k = 0; k < targets.size(); ++k )
  {
    if ( depths[k] >= controls.size() )
      continue;
    const wasm_control_frame_t &frame = controls[controls.size() - 1 - depths[k]];
    add_branch_resume(cdg.insn.ea, targets[k], frame, &resume_states);
  }
  if ( targets.size() == 1 )
  {
    mop_t default_target = by_serial.back() ? mop_t{} : direct_targets.back();
    if ( by_serial.back() )
      default_target.make_blkref(target_serials.back());
    return cdg.emit(m_goto, &default_target, nullptr, nullptr) != nullptr ? MERR_OK : MERR_INTERR;
  }
  // Locate the codegen's current block (it owns the selector spill we
  // just emitted; the switch bounds block gets wired back to it below).
  mblock_t *head = nullptr;
  for ( int m = cdg.mba->qty - 1; m >= 0 && head == nullptr; --m )
  {
    mblock_t *block = cdg.mba->get_mblock(m);
    for ( minsn_t *insn = block->head; insn != nullptr; insn = insn->next )
      if ( insn->opcode == m_mov && insn->d.t == mop_r && insn->d.r == branch_table_selector.r )
      {
        // The spill may load the selector from a global (or any
        // source) - only the DESTINATION register identifies it.
        head = block;
        break;
      }
  }
  if ( head == nullptr )
  {
    // When the selector spill was skipped (emit_move elides
    // src==dst moves, e.g. a selector already living in its kreg from
    // local.get) there is no mov to find - wire the chain to the block
    // the codegen is currently filling, which is where the bridge goto
    // below is emitted anyway.
    head = cdg.mb;
    if ( head == nullptr )
      return MERR_INTERR;
  }
  const merror_t switched
    = emit_br_table_m_jtbl(cdg, targets, by_serial, target_serials, direct_targets, head);
  if ( switched != MERR_OK )
  {
    msg("WASMHEX_F5_UNSUPPORTED: m_jtbl lowering failed at %a (merror=%d)\n", cdg.insn.ea,
        int(switched));
    return switched;
  }
  return MERR_OK;
}

// Load one caller-cross-checked Go result from the incoming window.
// At every `return` site the Go epilogue has already restored the cached SP
// local (local 1) to the window base [SP_entry + 8], so slot `r` lives at
// [local1 + r] — exactly where the caller reads it after the SP refresh.
// Mirrors emit_fixed_memory's dynamic-address load path (wasm32 + fixed
// memory; the synthesized access is dynamic, so the constant-address
// branches and the 'B' bounds edge cannot apply).
merror_t wasmhex_filter_t::emit_go_result_load(codegen_t &cdg, const loader::go_result_slot_t &slot,
                                               uchar type)
{
  if ( !go_results_synthesis || slot.width == 0 || wasm_type_size(type) < slot.width )
    return MERR_BADARCH;
  // In go-frame mode the result slot [SP+frame_size+8+sp_offset] is
  // the virtual args cell at inargoff + sp_offset — load it directly as a
  // stack variable (no linear-memory access at all).
  if ( go_frame_mode )
  {
    const int64 arg_stkoff = int64(cdg.mba->inargoff) + int64(slot.sp_offset);
    if ( arg_stkoff < int64(cdg.mba->inargoff)
         || arg_stkoff + int64(slot.width) > int64(cdg.mba->fullsize) )
      return MERR_BADARCH;
    mop_t arg_cell;
    arg_cell.make_stkvar(cdg.mba, sval_t(arg_stkoff));
    arg_cell.size = int(slot.width);
    const int wide_size = wasm_type_size(type);
    mop_t loaded;
    if ( !(wide_size == int(slot.width) ? make_operand_register(cdg, type, stack.size(), &loaded)
                                        : make_scratch_register(cdg, int(slot.width), 1, &loaded)) )
      return MERR_MEM;
    if ( !emit_move(cdg, arg_cell, loaded, type == 0x7D || type == 0x7C) )
      return MERR_INTERR;
    if ( wide_size == int(slot.width) )
    {
      push(loaded, type);
      return MERR_OK;
    }
    mop_t result;
    if ( !make_operand_register(cdg, type, stack.size(), &result) )
      return MERR_MEM;
    if ( cdg.emit(m_xdu, &loaded, nullptr, &result) == nullptr )
      return MERR_INTERR;
    push(result, type);
    return MERR_OK;
  }
  if ( size_t(1) >= locals.size() || locals[1].type != 0x7F )
    return MERR_BADARCH; // the SP cache local must be the declared local 1
  if ( !alloc_local(cdg, 1, true) )
    return MERR_BADARCH;
  netnode wasm_node(wasmhex_node_name, 0, false);
  uchar memory_mode = 0;
  uint64 memory_bytes = 0;
  bool growable_memory = false;
  if ( wasm_node == BADNODE
       || wasm_node.supval(0, &memory_mode, sizeof(memory_mode), 'M') != sizeof(memory_mode)
       || (memory_mode != 1 && memory_mode != 2) || memory_mode == 2
       || !get_wasm_memory_extent(wasm_node, memory_mode, &memory_bytes, &growable_memory) )
  {
    return MERR_BADARCH;
  }
  wasm_value_t address;
  address.type = 0x7F;
  address.op = locals[1].storage;
  mop_t effective_address;
  bool constant_address = false;
  bool statically_trapping = false;
  if ( !make_effective_memory_address(cdg, address, 4, slot.sp_offset, slot.width, memory_bytes,
                                      &effective_address, &constant_address, &statically_trapping) )
    return MERR_MEM;
  if ( statically_trapping )
    return MERR_BADARCH; // a dynamic SP-based address can never prove this
  mop_t selector;
  if ( !make_wasm_memory_selector(&selector, cdg.insn.ea, bounds_mode, constant_address) )
    return MERR_BADARCH;
  const int result_size = wasm_type_size(type);
  mop_t loaded;
  if ( !(result_size == int(slot.width) ? make_operand_register(cdg, type, stack.size(), &loaded)
                                        : make_scratch_register(cdg, int(slot.width), 1, &loaded)) )
    return MERR_MEM;
  if ( cdg.emit(m_ldx, &selector, &effective_address, &loaded) == nullptr )
    return MERR_INTERR;
  if ( result_size == int(slot.width) )
  {
    push(loaded, type);
    return MERR_OK;
  }
  // Narrow slot: zero-extend to the displayed
  // i32 — matching how callers read the slot back (load8_u/load16_u).
  mop_t result;
  if ( !make_operand_register(cdg, type, stack.size(), &result) )
    return MERR_MEM;
  if ( cdg.emit(m_xdu, &loaded, nullptr, &result) == nullptr )
    return MERR_INTERR;
  push(result, type);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_return(codegen_t &cdg)
{
  // A caller-cross-checked Go function returns its REAL results from
  // the incoming window. The wasm body's `i32.const 0` (RETUNWIND dead push)
  // is discarded and each proven slot is re-loaded at [SP + frame_size + 8 +
  // slot] instead; the generic result plumbing below then moves the loaded
  // values into the return variable / registers / sret buffer untouched.
  if ( go_results_synthesis )
  {
    wasm_value_t dead_push;
    if ( stack.size() < 1 || !pop(0x7F, &dead_push) )
      return MERR_BADARCH;
    if ( go_result_slots.size() != size_t(function_results.size()) )
      return MERR_BADARCH;
    for ( size_t i = 0; i < go_result_slots.size(); ++i )
    {
      const merror_t load_error = emit_go_result_load(cdg, go_result_slots[i], function_results[i]);
      if ( load_error != MERR_OK )
        return load_error;
    }
  }
  if ( function_results.size() > 1 )
  {
    size_t words = 0;
    if ( !multi_result_word_count(function_results, &words, has_sret_pointer)
         || stack.size() < function_results.size() )
      return MERR_BADARCH;
    qvector<wasm_value_t> values;
    values.resize(function_results.size());
    for ( size_t i = function_results.size(); i != 0; --i )
    {
      if ( !pop(function_results[i - 1], &values[i - 1]) )
        return MERR_BADARCH;
    }
    size_t result_size = 0;
    for ( uchar type : function_results )
      result_size += wasm_type_size(type);
    if ( has_sret_pointer )
    {
      size_t byte_offset = 0;
      for ( size_t i = 0; i < function_results.size(); ++i )
      {
        const uchar type = function_results[i];
        const int size = wasm_type_size(type);
        if ( size == 0
             || !emit_sret_store(cdg, values[i].op, byte_offset, type == 0x7D || type == 0x7C) )
          return MERR_BADARCH;
        byte_offset += size;
      }
      return cdg.emit(m_ret, nullptr, nullptr, nullptr) == nullptr ? MERR_INTERR : MERR_OK;
    }
    const bool use_return_variable = cdg.mba->retvaridx >= 0 && result_size <= 16;
    size_t register_index = 0;
    size_t byte_offset = 0;
    for ( size_t i = 0; i < function_results.size(); ++i )
    {
      const uchar type = function_results[i];
      const int size = wasm_type_size(type);
      if ( size == 0 )
        return MERR_BADARCH;
      mop_t destination;
      if ( use_return_variable )
      {
        destination._make_lvar(cdg.mba, cdg.mba->retvaridx, byte_offset);
        destination.size = size;
      }
      else if ( size == 4 )
      {
        destination.make_reg(reg2mreg(multi_return_registers[register_index]), size);
      }
      else
      {
        destination.make_reg_pair(reg2mreg(multi_return_registers[register_index]),
                                  reg2mreg(multi_return_registers[register_index + 1]), 4);
      }
      if ( !emit_move(cdg, values[i].op, destination, type == 0x7D || type == 0x7C) )
        return MERR_BADARCH;
      register_index += size / 4;
      byte_offset += size;
    }
  }
  else if ( function_results.size() == 1 )
  {
    const uchar type = function_results[0];
    const int size = wasm_type_size(type);
    wasm_value_t value;
    if ( size == 0 || stack.size() < 1 || !pop(type, &value) )
      return MERR_BADARCH;
    mop_t destination;
    if ( cdg.mba->retvaridx >= 0 )
    {
      destination._make_lvar(cdg.mba, cdg.mba->retvaridx);
      destination.size = size;
    }
    else if ( !make_regloc_mop(cdg, function_type.retloc, size, &destination) )
    {
      return MERR_BADARCH;
    }
    if ( !emit_move(cdg, value.op, destination, type == 0x7D || type == 0x7C) )
      return MERR_BADARCH;
  }
  else if ( !function_type.rettype.is_void() )
    return MERR_BADARCH;
  stack.clear();
  unreachable = true;
  return cdg.emit(m_ret, nullptr, nullptr, nullptr) == nullptr ? MERR_INTERR : MERR_OK;
}

bool wasmhex_filter_t::ends_with(const char *str, const char *suffix)
{
  const size_t str_len = strlen(str);
  const size_t suffix_len = strlen(suffix);
  return str_len >= suffix_len && strcmp(str + str_len - suffix_len, suffix) == 0;
}

// Replace the entryPointLoop resume
// `br_table` (`switch (PC_B)`) with a single direct `m_goto` to the main
// entry block (`PC_B == 0`, i.e., `go_dispatch_targets[0]`). Secondary
// resume blocks (reachable only via `PC_B != 0` on goroutine resume) still
// receive their abstract-stack resume state so they decode without stack
// underflow, and Hex-Rays DCE prunes any resume block not targeted by an
// in-function direct JMP. Fail-closed: any depth or emit failure returns an
// explicit error with EA (no silent fallback).
merror_t wasmhex_filter_t::emit_go_dispatcher_replacement(codegen_t &cdg)
{
  if ( go_dispatch_targets.size() < 2 || go_dispatch_targets.size() != go_dispatch_depths.size() )
  {
    msg("WASMHEX_F5_UNSUPPORTED: Go dispatcher replacement has invalid table metadata at %a\n",
        cdg.insn.ea);
    return MERR_BADARCH;
  }
  for ( size_t i = 0; i < go_dispatch_depths.size(); ++i )
  {
    if ( go_dispatch_depths[i] >= controls.size() )
    {
      msg("WASMHEX_F5_UNSUPPORTED: Go dispatcher arm %zu depth %u exceeds control stack (%zu) "
          "at %a\n",
          i, go_dispatch_depths[i], controls.size(), cdg.insn.ea);
      return MERR_BADARCH;
    }
    const wasm_control_frame_t &frame = controls[controls.size() - 1 - go_dispatch_depths[i]];
    add_branch_resume(cdg.insn.ea, go_dispatch_targets[i], frame, &resume_states);
  }
  ea_t entry_target_ea = go_dispatch_targets[0];
  if ( entry_target_ea <= cdg.insn.ea )
  {
    entry_target_ea = BADADDR;
    for ( ea_t t : go_dispatch_targets )
      if ( t > cdg.insn.ea && t < entry_target_ea )
        entry_target_ea = t;
    if ( entry_target_ea == BADADDR )
    {
      msg("WASMHEX_F5_UNSUPPORTED: Go dispatcher replacement has no forward entry arm at %a\n",
          cdg.insn.ea);
      return MERR_BADARCH;
    }
  }
  mop_t target;
  target.make_gvar(entry_target_ea);
  if ( cdg.emit(m_goto, &target, nullptr, nullptr) == nullptr )
  {
    msg("WASMHEX_F5_UNSUPPORTED: Go dispatcher replacement failed to emit entry goto at %a\n",
        cdg.insn.ea);
    return MERR_INTERR;
  }
  stack.clear();
  unreachable = true;
  if ( options::debug("g2") )
    msg("G2T entry=%a dispatcher=%a replaced with direct goto %a (arms=%zu)\n", state_entry,
        cdg.insn.ea, entry_target_ea, go_dispatch_targets.size());
  return MERR_OK;
}

// A recorded const-JMP (`i32.const k;
// local.set PC_B; br entryPointLoop` — the constant and the store were
// already consumed by the apply() handlers) lowers to a direct edge into
// the resume block of dispatch case k. The dispatcher br_table queued the
// resume state for that target when it lowered (add_branch_resume is
// target-deduplicated, so a second forward add is a no-op); a backward
// target resolves to the already-decoded block like every other br.
merror_t wasmhex_filter_t::emit_go_direct_jmp(codegen_t &cdg, const go_jmp_t &record)
{
  if ( unreachable )
    return MERR_OK;
  const uint64 depth = cdg.insn.Op1.value;
  const bool trace = options::debug("g2"); // Env reads live in loader/options.cpp
  qvector<uchar> branch_types;
  if ( !load_control_types(DatabaseEA{ uint64(cdg.insn.ea) }, &branch_types)
       || !branch_types.empty() || depth >= controls.size() )
  {
    msg("WASMHEX_F5_UNSUPPORTED: Go direct-JMP at %a has invalid control types or depth %llu\n",
        cdg.insn.ea, depth);
    return MERR_BADARCH;
  }
  wasm_control_frame_t &frame_at_depth = controls[controls.size() - 1 - size_t(depth)];
  // The Go compiler emits two equivalent JMP shapes: a branch back to the
  // entryPointLoop (frame is the dispatcher loop) or a branch straight to
  // the resume point (depth equals the dispatcher's own case-k depth —
  // the br_table arms exit the nested blocks to the same resume EA).
  const bool via_loop = frame_at_depth.kind == 0x03 && frame_at_depth.start <= go_dispatch_ea
    && frame_at_depth.end >= go_dispatch_ea && stack_has_prefix(frame_at_depth.base_stack);
  const bool via_case = record.case_index < go_dispatch_depths.size()
    && go_dispatch_depths[record.case_index] == depth
    && stack_has_prefix(frame_at_depth.base_stack);
  if ( !via_loop && !via_case )
  {
    msg("WASMHEX_F5_UNSUPPORTED: Go direct-JMP at %a (depth=%llu, case=%u) does not target the "
        "entryPointLoop or matching case depth\n",
        cdg.insn.ea, depth, record.case_index);
    return MERR_BADARCH;
  }
  const ea_t target_ea = go_dispatch_targets[record.case_index];
  mop_t target;
  target.make_gvar(target_ea);
  if ( cdg.emit(m_goto, &target, nullptr, nullptr) == nullptr )
    return MERR_INTERR;
  stack = frame_at_depth.base_stack;
  unreachable = true;
  add_branch_resume(cdg.insn.ea, target_ea, frame_at_depth, &resume_states);
  ++go_direct_edges;
  if ( trace && go_direct_edges <= 16 )
    msg("G2T entry=%a edge#%zu br=%a k=%u direct goto %a (%s)\n", state_entry, go_direct_edges,
        cdg.insn.ea, record.case_index, target_ea, via_loop ? "loop" : "case-depth");
  return MERR_OK;
}
}
