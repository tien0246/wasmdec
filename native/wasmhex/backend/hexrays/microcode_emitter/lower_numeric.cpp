#include "filter.hpp"
#include "../../../backend/idb/metadata_schema/metadata.hpp"
namespace wasmhex
{
bool wasmhex_filter_t::emit_move(codegen_t &cdg, const mop_t &src, const mop_t &dst, bool fp)
{
  if ( src == dst )
    return true;
  minsn_t *ins = cdg.emit(m_mov, &src, nullptr, &dst);
  if ( ins != nullptr && fp )
    ins->set_fpinsn();
  return ins != nullptr;
}

merror_t wasmhex_filter_t::emit_drop(codegen_t &cdg)
{
  if ( stack.empty() )
    return MERR_BADARCH;
  // Dropping a C-frame marker is dead code - the value is unused
  // so fall through instead of failing the whole function.
  stack.pop_back();
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_select(codegen_t &cdg, uchar expected_type)
{
  if ( stack.size() < 3 || stack.back().type != 0x7F )
    return MERR_BADARCH;
  const uchar value_type = stack[stack.size() - 2].type;
  if ( stack[stack.size() - 3].type != value_type
       || (expected_type != 0 && value_type != expected_type)
       || (value_type != 0x7F && value_type != 0x7E && value_type != 0x7D && value_type != 0x7C
           && value_type != 0x70 && value_type != 0x6F && value_type != 0x71
           && value_type != 0x72) )
    return MERR_BADARCH;
  const int size = wasm_type_size(value_type);
  if ( size != 4 && size != 8 )
    return MERR_BADARCH;

  wasm_value_t condition, second, first;
  const bool allow_marker = has_c_frame && value_type == 0x7F;
  if ( !pop(0x7F, &condition) || !pop(value_type, &second, allow_marker)
       || !pop(value_type, &first, allow_marker) )
    return MERR_BADARCH;
  // Select between two address values - materialize markers.
  if ( is_c_frame_marker(second) || is_c_frame_marker(first) )
  {
    if ( !materialize_c_frame_marker(cdg, &second) || !materialize_c_frame_marker(cdg, &first) )
      return reject_c_frame_marker_use(cdg, "marker select out of frame");
  }
  const char *helper = value_type == 0x7F ? "wasm_select_i32"
    : value_type == 0x7E                  ? "wasm_select_i64"
    : value_type == 0x7D                  ? "wasm_select_f32"
    : value_type == 0x7C                  ? "wasm_select_f64"
                                          : "wasm_select_ref";
  const uchar argument_types[] = { first.type, second.type, 0x7F };
  push(first.op, first.type);
  push(second.op, second.type);
  push(condition.op, 0x7F);
  return emit_call(cdg, helper, 3, value_type, false, argument_types);
}

merror_t wasmhex_filter_t::emit_constant(codegen_t &cdg, uchar type)
{
  const bool is_float = type == 0x7D || type == 0x7C;
  const uint64_t bits = uint64(cdg.insn.Op1.value);
  const bool subnormal = is_float
    && (type == 0x7D ? (bits & 0x7f800000u) == 0 && (bits & 0x007fffffu) != 0
                     : (bits & 0x7ff0000000000000ull) == 0 && (bits & 0x000fffffffffffffull) != 0);
  mop_t constant;
  if ( is_float && !subnormal )
  {
    if ( !constant.make_fpnum(&cdg.insn.Op1.value, type == 0x7D ? 4 : 8) )
      return MERR_BADARCH;
  }
  else
  {
    constant.make_number(bits, type == 0x7F || type == 0x7D ? 4 : 8, cdg.insn.ea, 0);
  }
  if ( !subnormal )
    push(constant, type);
  else
  {
    mop_t destination;
    if ( !make_operand_register(cdg, type, stack.size(), &destination) )
      return MERR_MEM;
    if ( !emit_move(cdg, constant, destination, false) )
      return MERR_INTERR;
    push(destination, type);
  }
  stack.back().constant = bits;
  stack.back().has_constant = true;
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_add(codegen_t &cdg, uchar type)
{
  wasm_value_t right, left;
  // The Go computed-address shape runs the fold in i64
  // (extend/add/wrap) — markers must pop at i64 too (go-frame only).
  const bool allow_marker = has_c_frame && (type == 0x7F || go_frame_mode);
  if ( !pop(type, &right, allow_marker) || !pop(type, &left, allow_marker) )
    return MERR_BADARCH;
  if ( is_c_frame_marker(left) || is_c_frame_marker(right) )
  {
    if ( fold_c_frame_arithmetic(cdg, left, right, false) )
      return MERR_OK;
    const int op_size = type == 0x7E ? 8 : 4;
    if ( !materialize_c_frame_marker(cdg, &left) )
    {
      mop_t raw;
      if ( lower_c_frame_marker_address(cdg, left, stack.size(), &raw) )
        left.op = raw;
      else
        return reject_c_frame_marker_use(cdg, "marker add out of frame");
    }
    if ( !widen_materialized_address(cdg, &left.op, op_size, 2) )
      return MERR_MEM;
    if ( !materialize_c_frame_marker(cdg, &right) )
    {
      mop_t raw;
      if ( lower_c_frame_marker_address(cdg, right, stack.size(), &raw) )
        right.op = raw;
      else
        return reject_c_frame_marker_use(cdg, "marker add out of frame");
    }
    if ( !widen_materialized_address(cdg, &right.op, op_size, 3) )
      return MERR_MEM;
  }
  if ( is_table_base_marker(left) || is_table_base_marker(right) )
  {
    if ( fold_table_base_arithmetic(cdg, left, right, false) )
      return MERR_OK;
  }
  const int size = type == 0x7F || type == 0x7D ? 4 : 8;
  mop_t destination;
  if ( !make_operand_register(cdg, type, stack.size(), &destination) )
    return MERR_MEM;
  const mcode_t opcode = type == 0x7F || type == 0x7E ? m_add : m_fadd;
  minsn_t *ins = cdg.emit(opcode, &left.op, &right.op, &destination);
  if ( ins == nullptr )
    return MERR_INTERR;
  if ( opcode == m_fadd )
    ins->set_fpinsn();
  push(destination, type);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_float_binary(codegen_t &cdg, uchar type, mcode_t opcode)
{
  wasm_value_t right, left;
  if ( !pop(type, &right) || !pop(type, &left) )
    return MERR_BADARCH;
  mop_t destination;
  if ( !make_operand_register(cdg, type, stack.size(), &destination) )
    return MERR_MEM;
  minsn_t *ins = cdg.emit(opcode, &left.op, &right.op, &destination);
  if ( ins == nullptr )
    return MERR_INTERR;
  ins->set_fpinsn();
  push(destination, type);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_integer_binary(codegen_t &cdg, uchar type, mcode_t opcode)
{
  wasm_value_t right, left;
  // The Go computed-address chain folds markers at i64 —
  // `i64.sub` appears on frame-relative sentinels (internal_strconv.fmtX
  // end-pointer math), so markers pop at i64 integer ops in go-frame mode.
  const bool allow_marker = has_c_frame && (type == 0x7F || go_frame_mode);
  if ( !pop(type, &right, allow_marker) || !pop(type, &left, allow_marker) )
    return MERR_BADARCH;
  if ( is_c_frame_marker(left) || is_c_frame_marker(right) )
  {
    if ( opcode == m_sub && fold_c_frame_arithmetic(cdg, left, right, true) )
      return MERR_OK;
    // C-frame fix: integer ops on C-frame markers beyond +/- folding are
    // legal C codegen — pointer tagging (&p | k), masking, shifts. Resolve
    // each side to its real address (in-frame stack slot, else
    // sp_global + offset) and run the plain integer op on the value.
    if ( !resolve_c_frame_scalar(cdg, &left, stack.size())
         || !resolve_c_frame_scalar(cdg, &right, stack.size()) )
    {
      return reject_c_frame_marker_use(cdg, "marker integer operand out of frame");
    }
    // Widen the 4-byte materialized address to the op width.
    if ( type == 0x7E )
    {
      if ( !widen_materialized_address(cdg, &left.op, 8, 2)
           || !widen_materialized_address(cdg, &right.op, 8, 3) )
        return MERR_MEM;
    }
  }
  const int size = type == 0x7F || type == 0x7D ? 4 : 8;
  if ( is_table_base_marker(left) || is_table_base_marker(right) )
  {
    if ( fold_table_base_arithmetic(cdg, left, right, true) )
      return MERR_OK;
  }
  mop_t destination;
  if ( left.owned_temp && left.op.is_stkvar() )
    destination = left.op;
  else if ( !make_operand_register(cdg, type, stack.size(), &destination) )
    return MERR_MEM;
  if ( cdg.emit(opcode, &left.op, &right.op, &destination) == nullptr )
    return MERR_INTERR;
  push(destination, type, UINT32_MAX, true);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_i64_shift(codegen_t &cdg, mcode_t opcode)
{
  wasm_value_t count;
  if ( !pop(0x7E, &count) )
    return MERR_BADARCH;
  uint64 raw_count = count.constant;
  if ( !count.has_constant && !count.op.is_constant(&raw_count, false) )
  {
    // Experiment note: lowering the dynamic-count shift natively
    // (m_and count,63 + m_low + m_shr) verifies cleanly but the engine's
    // variable allocator then splits MANY more 64-bit shift results into
    // half-lvar pairs (SET_PAIR 5 -> 20 on yyjson) than the helper path,
    // because the pair-returning call kept the halves fused. Keep the
    // helper here; the pair debt is tracked against the ABI (every 8-byte
    // return travels as @<r0:r1>).
    push(count.op, 0x7E);
    const uchar argument_types[] = { 0x7E, 0x7E };
    return emit_typed_helper_from_instruction(cdg, 2, argument_types, 0x7E, false);
  }
  if ( (raw_count & 63) >= 32 )
  {
    // Constant 32..63 keeps the helper as well — a native m_shr with
    // a >=32 constant is legal microcode but reproduces the same
    // half-split allocation noise on the consumers.
    push(count.op, 0x7E);
    const uchar argument_types[] = { 0x7E, 0x7E };
    return emit_typed_helper_from_instruction(cdg, 2, argument_types, 0x7E, false);
  }
  wasm_value_t value;
  if ( !pop(0x7E, &value) )
    return MERR_BADARCH;
  mop_t destination;
  if ( !make_operand_register(cdg, 0x7E, stack.size(), &destination) )
    return MERR_MEM;
  mop_t shift_count;
  shift_count.make_number(raw_count & 63, 1, cdg.insn.ea, 0);
  if ( cdg.emit(opcode, &value.op, &shift_count, &destination) == nullptr )
    return MERR_INTERR;
  push(destination, 0x7E);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_i32_shift(codegen_t &cdg, mcode_t opcode)
{
  wasm_value_t count;
  if ( !pop(0x7F, &count) )
    return MERR_BADARCH;
  uint64 raw_count = count.constant;
  if ( !count.has_constant && !count.op.is_constant(&raw_count, false) )
  {
    // Keep the helper (see the i64 path note — the engine's allocator
    // splits native 64-bit shift results into half-lvar pairs far more
    // often than the fused pair-returning call did).
    push(count.op, 0x7F);
    const uchar argument_types[] = { 0x7F, 0x7F };
    return emit_typed_helper_from_instruction(cdg, 2, argument_types, 0x7F, false);
  }
  wasm_value_t value;
  if ( !pop(0x7F, &value) )
    return MERR_BADARCH;
  mop_t destination;
  if ( !make_operand_register(cdg, 0x7F, stack.size(), &destination) )
    return MERR_MEM;
  mop_t shift_count;
  shift_count.make_number(raw_count & 31, 1, cdg.insn.ea, 0);
  if ( cdg.emit(opcode, &value.op, &shift_count, &destination) == nullptr )
    return MERR_INTERR;
  push(destination, 0x7F);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_integer_conversion(codegen_t &cdg, uchar input_type,
                                                   uchar output_type, mcode_t opcode,
                                                   int input_width)
{
  wasm_value_t value;
  // C-frame fix: i64.extend_i32_u on a C-frame marker is the standard
  // syscall-wrapper shape (pointer widened for the wasi ABI) — resolve
  // the marker to its real address and widen that value.
  const bool allow_marker
    = has_c_frame && (input_type == 0x7F || (go_frame_mode && opcode == m_low));
  if ( !pop(input_type, &value, allow_marker) )
    return MERR_BADARCH;
  // The wrap of a proven pointer-receiver member access
  // keeps the 8-byte receiver value (typed-pointer route for the member
  // load; the flat xdu would break the engine's struct-offset analysis at
  // every non-zero member offset). The declared abstract-stack type stays
  // 4-byte so the following pops stay balanced; the marker routes the
  // member load to an m_ldx over the 8-byte pointer.
  if ( opcode == m_low && cdg.insn.itype == wasmhex_i32_wrap_i64
       && go_receiver_wrap_eas.count(cdg.insn.ea) != 0 )
  {
    wasm_value_t &receiver = stack.push_back();
    receiver = value;
    receiver.type = output_type;
    receiver.is_go_receiver_address = true;
    receiver.has_constant = false;
    return MERR_OK;
  }
  if ( is_c_frame_marker(value) )
  {
    // The Go computed-address shape widens the frame anchor
    // (i64.extend_i32_u), folds the window offset (i64.add), then wraps
    // back (i32.wrap_i64) — propagate the marker through the width
    // changes (offsets stay < 2^31) instead of materializing, so the
    // address stays frame-relative for the memory lowering.
    if ( go_frame_mode && (opcode == m_xdu || opcode == m_low) )
    {
      wasm_value_t &marker = stack.push_back();
      marker.type = output_type;
      marker.is_c_frame_address = true;
      marker.c_frame_offset = value.c_frame_offset;
      return MERR_OK;
    }
    if ( !resolve_c_frame_scalar(cdg, &value, stack.size()) )
      return reject_c_frame_marker_use(cdg, "marker integer conversion out of frame");
  }
  const int size = output_type == 0x7E ? 8 : 4;
  mop_t destination;
  if ( !make_operand_register(cdg, output_type, stack.size(), &destination) )
    return MERR_MEM;
  mop_t source = value.op;
  if ( input_width != 0 )
    source.size = input_width;
  if ( cdg.emit(opcode, &source, nullptr, &destination) == nullptr )
    return MERR_INTERR;
  push(destination, output_type);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_eqz(codegen_t &cdg, uchar type)
{
  wasm_value_t value;
  // The Go computed-address chain folds markers at i64 —
  // `i64.eqz` guards frame-relative sentinels on wasm-test/main.wasm
  // (wasm_func_3262), so markers pop at i64 eqz in go-frame mode too.
  const bool allow_marker = has_c_frame && (type == 0x7F || go_frame_mode);
  if ( !pop(type, &value, allow_marker) )
    return MERR_BADARCH;
  if ( is_c_frame_marker(value) )
  {
    if ( !materialize_c_frame_marker(cdg, &value) )
    {
      mop_t raw_address;
      if ( !lower_c_frame_marker_address(cdg, value, stack.size(), &raw_address) )
        return reject_c_frame_marker_use(cdg, "marker eqz out of frame");
      value.op = raw_address;
    }
    // Widen the 4-byte materialized address to the operand
    // width (the same rule as every other 8-byte marker consumer).
    if ( !widen_materialized_address(cdg, &value.op, type == 0x7E ? 8 : 4, 2) )
      return MERR_MEM;
  }
  mop_t boolean, destination;
  if ( !make_boolean_register(cdg, &boolean)
       || !make_operand_register(cdg, 0x7F, stack.size(), &destination) )
    return MERR_MEM;
  mop_t zero;
  zero.make_number(0, type == 0x7E ? 8 : 4, cdg.insn.ea, 0);
  if ( cdg.emit(m_setz, &value.op, &zero, &boolean) == nullptr )
    return MERR_INTERR;
  if ( cdg.emit(m_xdu, &boolean, nullptr, &destination) == nullptr )
    return MERR_INTERR;
  push(destination, 0x7F);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_compare(codegen_t &cdg, uchar type, mcode_t opcode,
                                        bool float_compare)
{
  wasm_value_t right, left;
  const bool allow_marker = has_c_frame && (type == 0x7F || go_frame_mode);
  if ( !pop(type, &right, allow_marker) || !pop(type, &left, allow_marker) )
    return MERR_BADARCH;
  if ( is_c_frame_marker(left) || is_c_frame_marker(right) )
  {
    // Address comparisons (&p == &q). C-frame fix: an in-frame
    // marker materializes as a stack-slot address; anything else (sp
    // base, negative or past-the-end offsets) resolves to the real
    // sp_global + offset linear address instead of failing the function.
    if ( !resolve_c_frame_scalar(cdg, &left, stack.size())
         || !resolve_c_frame_scalar(cdg, &right, stack.size()) )
    {
      return reject_c_frame_marker_use(cdg, "marker compare out of frame");
    }
    // A materialized frame-cell address is 4 bytes on
    // wasm32 — an i64 compare operand must widen once (the same rule as
    // every other 8-byte marker consumer).
    if ( type == 0x7E )
    {
      if ( !widen_materialized_address(cdg, &left.op, 8, 2)
           || !widen_materialized_address(cdg, &right.op, 8, 3) )
        return MERR_MEM;
    }
  }
  // Float NaN checks compare an operand with itself (x != x); a setcc
  // whose two source mops are identical trips the microcode verifier.
  // Spill one side through a scratch register first.
  if ( float_compare && left.op == right.op )
  {
    mop_t temp;
    if ( !make_operand_register(cdg, type, stack.size() + 1, &temp) )
      return MERR_MEM;
    if ( !emit_move(cdg, right.op, temp, true) )
      return MERR_INTERR;
    right.op = temp;
  }
  mop_t boolean, destination;
  if ( !make_boolean_register(cdg, &boolean)
       || !make_operand_register(cdg, 0x7F, stack.size(), &destination) )
    return MERR_MEM;
  minsn_t *comparison = cdg.emit(opcode, &left.op, &right.op, &boolean);
  if ( comparison == nullptr )
    return MERR_INTERR;
  if ( float_compare )
    comparison->set_fpinsn();
  minsn_t *wide = cdg.emit(m_xdu, &boolean, nullptr, &destination);
  if ( wide == nullptr )
    return MERR_INTERR;
  push(destination, 0x7F);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_float_bits_helper(codegen_t &cdg, const char *helper,
                                                  int argument_count, uchar float_type,
                                                  uchar result_type, bool result_is_float)
{
  if ( argument_count < 1 || argument_count > 2 || (float_type != 0x7D && float_type != 0x7C)
       || (result_type != 0x7F && result_type != 0x7E) )
    return MERR_BADARCH;

  const uchar bits_type = float_type == 0x7D ? 0x7F : 0x7E;
  qvector<wasm_value_t> values;
  values.resize(argument_count);
  for ( int i = argument_count; i != 0; --i )
    if ( !pop(float_type, &values[i - 1]) )
      return MERR_BADARCH;

  uchar argument_types[2] = { bits_type, bits_type };
  for ( int i = 0; i < argument_count; ++i )
    push(values[i].op, bits_type);
  const merror_t error = emit_call(cdg, helper, argument_count, result_type, false, argument_types);
  if ( error != MERR_OK || !result_is_float )
    return error;

  wasm_value_t result;
  if ( !pop(result_type, &result) )
    return MERR_BADARCH;
  push(result.op, float_type);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_float_bits_helper(codegen_t &cdg, int argument_count,
                                                  uchar float_type, uchar result_type,
                                                  bool result_is_float)
{
  std::string helper_name = "wasm_";
  for ( const char *p = instructions[cdg.insn.itype].name; *p != '\0'; ++p )
    helper_name.push_back(*p == '.' ? '_' : *p);
  helper_name += "_bits";
  return emit_float_bits_helper(cdg, helper_name.c_str(), argument_count, float_type, result_type,
                                result_is_float);
}

merror_t wasmhex_filter_t::emit_typed_helper(codegen_t &cdg, const char *helper, int argument_count,
                                             const uchar *argument_types, uchar result_type)
{
  if ( helper == nullptr || argument_count < 1 || argument_count > 3 || argument_types == nullptr )
  {
    msg("WASMHEX_TYPED_HELPER_UNSUPPORTED: invalid signature at %a\n", cdg.insn.ea);
    return MERR_BADARCH;
  }
  qvector<wasm_value_t> values;
  values.resize(argument_count);
  for ( int i = argument_count; i != 0; --i )
    if ( !pop(argument_types[i - 1], &values[i - 1]) )
    {
      msg("WASMHEX_TYPED_HELPER_UNSUPPORTED: argument %d at %a\n", i - 1, cdg.insn.ea);
      return MERR_BADARCH;
    }
  for ( int i = 0; i < argument_count; ++i )
  {
    if ( wasm_type_size(argument_types[i]) == 0 )
      return MERR_BADARCH;
    push(values[i].op, argument_types[i]);
  }
  const merror_t call_error
    = emit_call(cdg, helper, argument_count, result_type, false, argument_types);
  if ( call_error != MERR_OK )
    msg("WASMHEX_TYPED_HELPER_UNSUPPORTED: call error %d at %a\n", int(call_error), cdg.insn.ea);
  return call_error;
}

merror_t wasmhex_filter_t::emit_typed_helper_from_instruction(codegen_t &cdg, int argument_count,
                                                              const uchar *argument_types,
                                                              uchar result_type, bool bits_helper,
                                                              const char *helper_prefix)
{
  std::string helper_name = helper_prefix;
  for ( const char *p = instructions[cdg.insn.itype].name; *p != '\0'; ++p )
    helper_name.push_back(*p == '.' ? '_' : *p);
  if ( bits_helper )
    helper_name += "_bits";
  return emit_typed_helper(cdg, helper_name.c_str(), argument_count, argument_types, result_type);
}

merror_t wasmhex_filter_t::emit_float_conversion(codegen_t &cdg, uchar input_type,
                                                 uchar output_type, mcode_t opcode)
{
  wasm_value_t value;
  if ( !pop(input_type, &value) )
    return MERR_BADARCH;
  mop_t destination;
  if ( !make_operand_register(cdg, output_type, stack.size(), &destination) )
    return MERR_MEM;
  minsn_t *conversion = cdg.emit(opcode, &value.op, nullptr, &destination);
  if ( conversion == nullptr )
    return MERR_INTERR;
  conversion->set_fpinsn();
  push(destination, output_type);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_local(codegen_t &cdg, bool get, bool tee)
{
  const size_t index = size_t(cdg.insn.Op1.value);
  if ( index >= locals.size() )
    return MERR_BADARCH;
  wasm_local_t &local = locals[index];
  const int size = int(local.tif.get_size());
  if ( size != 4 && size != 8 )
    return MERR_BADARCH;
  const bool is_c_fp_local = has_c_frame && index == size_t(c_frame_metadata.fp_local_index);
  if ( get )
  {
    if ( is_c_fp_local )
    {
      if ( !c_frame_fp_written )
        return reject_c_frame_marker_use(cdg, "local.get fp before prologue");
      push_c_frame_address(0);
      return MERR_OK;
    }
    if ( !alloc_local(cdg, index, true) )
      return MERR_BADARCH;
    if ( local.storage.is_stkvar() )
    {
      mop_t value;
      if ( !make_operand_register(cdg, local.type, stack.size(), &value) )
        return MERR_MEM;
      if ( !emit_move(cdg, local.storage, value, local.tif.is_floating()) )
        return MERR_INTERR;
      push(value, local.type, local.function_ref_index);
    }
    else
      push(local.storage, local.type, local.function_ref_index);
    return MERR_OK;
  }

  if ( is_c_fp_local )
  {
    wasm_value_t value;
    if ( go_frame_mode )
    {
      // Writes to the Go SP-cache local are either the (consumed)
      // prologue/epilogue legs, the (consumed) call refresh pairs, or a
      // plain `global.get 0; local.set L` cache init — in every case the
      // invariant holds (SP == fp between calls), so accept the write and
      // keep the anchor. The popped marker may carry the i64 type (the
      // computed-address chain widens before folding), so pop by the
      // value's own type, not the local's.
      if ( stack.empty() )
        return MERR_BADARCH;
      wasm_value_t value = stack.back();
      if ( value.type != local.type && value.type != 0x7E && value.type != 0x7F )
        return MERR_BADARCH;
      stack.pop_back();
      if ( value.is_c_frame_address && value.c_frame_offset != 0 )
      {
        // A real SP adjustment (alloca-like) the frame model cannot
        // express — fail closed, measurable.
        return reject_c_frame_marker_use(cdg, "Go SP cache local re-anchored mid-body");
      }
      // Plain value (a `global.get 0` cache init — SP == fp) or a zero
      // marker: the anchor is unchanged; discard the value.
      if ( tee )
        push_c_frame_address(0);
      return MERR_OK;
    }
    if ( c_frame_fp_written || !pop(local.type, &value, true) || !value.is_c_frame_address
         || value.c_frame_offset != 0 )
      return reject_c_frame_marker_use(cdg, "non-prologue write to C frame pointer local");
    c_frame_fp_written = true;
    local.initialized = true;
    if ( tee )
      push_c_frame_address(0);
    return MERR_OK;
  }

  wasm_value_t value;
  // `int *p = &local;` lowers to local.set/tee of a C-frame
  // address marker; materialize it as an explicit frame-slot address so
  // the local carries a real pointer value from here on.
  if ( !pop(local.type, &value, true) || !alloc_local(cdg, index, false) )
    return MERR_BADARCH;
  if ( soft_c_frame && is_c_frame_marker(value) )
  {
    // Ticket-1 fix: the prologue parks the frame pointer in a local the
    // loader did not accept ("fp is not a declared i32 local" fallback).
    // Soft dataflow tracking cannot learn a frame through that local -
    // turn soft tracking off for this function and let the marker
    // degrade through the sp-global address like before.
    soft_c_frame = false;
    has_c_frame = false;
  }
  bool materialized_marker = false;
  if ( is_c_frame_marker(value) )
  {
    // An out-of-frame address local degrades to the sp-based
    // linear-memory address instead of failing the whole function.
    materialized_marker = true;
    if ( !materialize_c_frame_marker(cdg, &value) )
    {
      mop_t raw_address;
      if ( !lower_c_frame_marker_address(cdg, value, stack.size(), &raw_address) )
        return reject_c_frame_marker_use(cdg, "local.set of out-of-frame address");
      value.op = raw_address;
    }
  }
  // A materialized frame-cell address is 4 bytes on
  // wasm32 — moving it into an 8-byte local (the Go pointer temps are
  // i64) is an m_mov size mismatch. Widen once.
  if ( materialized_marker && !widen_materialized_address(cdg, &value.op, size, 2) )
    return MERR_MEM;
  for ( size_t i = 0; i < stack.size(); ++i )
  {
    if ( stack[i].op != local.storage )
      continue;
    mop_t snapshot;
    if ( !make_operand_register(cdg, local.type, i, &snapshot) )
      return MERR_MEM;
    if ( !emit_move(cdg, stack[i].op, snapshot, local.tif.is_floating()) )
      return MERR_INTERR;
    stack[i].op = snapshot;
  }
  if ( !emit_move(cdg, value.op, local.storage, local.tif.is_floating()) )
    return MERR_INTERR;
  local.initialized = true;
  local.function_ref_index = local.type == 0x70 ? value.function_ref_index : UINT32_MAX;
  if ( tee )
    push(local.storage, local.type, local.function_ref_index);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_global(codegen_t &cdg, bool set)
{
  if ( cdg.insn.Op1.value > UINT32_MAX )
    return MERR_BADARCH;
  const uint32 global_index = uint32(cdg.insn.Op1.value);
  if ( go_frame_mode && has_c_frame && global_index == c_frame_metadata.sp_global_index )
  {
    // The Go frame anchor comes from the SP-cache local, not the
    // global — `global.get 0` outside the consumed legs is the morestack
    // guard reading the plain global (fall through to the generic path);
    // any `global.set 0` reaching here is an unmodeled SP update (every
    // protocol leg is consumed), so fail writes closed.
    if ( set )
      return reject_c_frame_marker_use(cdg, "unmodeled global.set on Go stack pointer");
  }
  else if ( has_c_frame && global_index == c_frame_metadata.sp_global_index )
  {
    if ( !set )
    {
      if ( c_frame_fp_written )
        return reject_c_frame_marker_use(cdg, "non-prologue global.get on C stack pointer");
      // Dataflow frame: after the first sp update the global
      // holds the frame anchor - a second read is the frame pointer.
      if ( c_frame_sp_read && soft_c_frame && soft_frame_learned )
      {
        push_c_frame_address(0);
        return MERR_OK;
      }
      if ( c_frame_sp_read )
        return reject_c_frame_marker_use(cdg, "non-prologue global.get on C stack pointer");
      c_frame_sp_read = true;
      push_c_sp_base();
      return MERR_OK;
    }
    wasm_value_t value;
    if ( !pop(0x7F, &value, true) )
      return reject_c_frame_marker_use(cdg, "non-marker global.set on C stack pointer");
    if ( !value.is_c_frame_address )
    {
      // A dynamic alloca degenerates the new sp to a raw
      // address; store it to the global like a plain assignment and let
      // later frame accesses degrade naturally.
      if ( !soft_c_frame )
        return reject_c_frame_marker_use(cdg, "non-marker global.set on C stack pointer");
      mop_t sp_gvar;
      uchar sp_type = 0, sp_mut = 0;
      ea_t sp_address = BADADDR;
      bool sp_imported = false;
      qstring sp_helper;
      if ( get_global_metadata(c_frame_metadata.sp_global_index, &sp_type, &sp_mut, &sp_address,
                               &sp_imported, &sp_helper)
           && sp_address != BADADDR )
      {
        sp_gvar.make_gvar(sp_address);
        sp_gvar.size = 4;
        return emit_move(cdg, value.op, sp_gvar, false) ? MERR_OK : MERR_INTERR;
      }
      return MERR_BADARCH;
    }
    const int32 expected_epilogue = c_frame_metadata.direction < 0
      ? int32(c_frame_metadata.c_frame_bytes)
      : -int32(c_frame_metadata.c_frame_bytes);
    if ( value.c_frame_offset != 0 && value.c_frame_offset != expected_epilogue )
      return reject_c_frame_marker_use(cdg, "mismatched global.set delta on C stack pointer");
    return MERR_OK;
  }
  uchar type = 0, mutability = 0;
  ea_t address = BADADDR;
  bool imported = false;
  qstring import_helper;
  if ( !get_global_metadata(uint32(cdg.insn.Op1.value), &type, &mutability, &address, &imported,
                            &import_helper)
       || (set && !mutability) )
    return MERR_BADARCH;
  const int size = wasm_type_size(type);
  if ( size != 4 && size != 8 )
    return MERR_BADARCH;
  bool is_tb = false;
  if ( !set && type == 0x7F )
  {
    if ( address != BADADDR )
    {
      qstring gname;
      get_name(&gname, address);
      if ( gname == "__table_base" || gname == "_table_base" )
        is_tb = true;
    }
    if ( !is_tb && !import_helper.empty() )
    {
      if ( import_helper.find("__table_base") != qstring::npos
           || import_helper.find("_table_base") != qstring::npos )
        is_tb = true;
    }
  }
  if ( address == BADADDR )
  {
    if ( !imported || import_helper.empty() )
      return MERR_BADARCH;
    qstring helper(import_helper);
    const uchar argument_types[] = { type };
    if ( set )
    {
      helper.append("_set");
      return emit_call(cdg, helper.c_str(), 1, 0x40, false, argument_types);
    }
    helper.append("_get");
    return emit_call(cdg, helper.c_str(), 0, type, false, argument_types);
  }
  mop_t global;
  global.make_gvar(address);
  global.size = size;
  const bool is_float = type == 0x7D || type == 0x7C;
  if ( set )
  {
    wasm_value_t value;
    // A Go computed address may land in a plain global — the
    // PC_F indirect-call protocol stores the closure context (global 1)
    // from the same extend/add chain, so the popped value can be a
    // frame-address marker. Materialize it (&stkvar or the raw sp+k
    // linear address) and widen to the global's width.
    if ( !pop(type, &value, has_c_frame && go_frame_mode) )
      return MERR_BADARCH;
    if ( is_c_frame_marker(value) )
    {
      if ( !materialize_c_frame_marker(cdg, &value) )
      {
        mop_t raw_address;
        if ( !lower_c_frame_marker_address(cdg, value, stack.size(), &raw_address) )
          return reject_c_frame_marker_use(cdg, "global.set of out-of-frame address");
        value.op = raw_address;
      }
      if ( !widen_materialized_address(cdg, &value.op, size, 2) )
        return MERR_MEM;
    }
    return emit_move(cdg, value.op, global, is_float) ? MERR_OK : MERR_INTERR;
  }

  mop_t destination;
  if ( !make_operand_register(cdg, type, stack.size(), &destination) )
    return MERR_MEM;
  if ( !emit_move(cdg, global, destination, is_float) )
    return MERR_INTERR;
  push(destination, type);
  if ( is_tb )
  {
    stack.back().is_table_base = true;
    stack.back().table_base_offset = 0;
  }
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_br(codegen_t &cdg, bool conditional)
{
  if ( unreachable )
    return MERR_OK;
  const uint64 depth = cdg.insn.Op1.value;
  wasm_value_t condition;
  qvector<uchar> branch_types;
  if ( !load_control_types(DatabaseEA{ uint64(cdg.insn.ea) }, &branch_types)
       || (conditional && !pop(0x7F, &condition)) )
    return MERR_BADARCH;
  if ( depth >= controls.size() )
    return MERR_BADARCH;
  wasm_control_frame_t &target_frame = controls[controls.size() - 1 - size_t(depth)];
  if ( branch_types != target_frame.label_types
       || target_frame.label_values.size() != branch_types.size()
       || !stack_has_prefix(target_frame.base_stack)
       || stack.size() < target_frame.base_stack.size() + branch_types.size() )
    return MERR_BADARCH;
  for ( size_t i = 0; i < branch_types.size(); ++i )
  {
    wasm_value_t &branch_value = stack[stack.size() - branch_types.size() + i];
    if ( branch_value.type != branch_types[i] )
      return MERR_BADARCH;
    if ( is_c_frame_marker(branch_value) )
    {
      // An address payload (`br (result i32) (fp + k)`) is a plain
      // pointer value in C - materialize it and let the normal payload
      // moves carry the explicit stack-slot address.
      if ( !materialize_c_frame_marker(cdg, &branch_value) )
        return reject_c_frame_marker_use(cdg, "branch payload");
    }
  }
  qvector<ea_t> targets;
  if ( !get_ea_list(cdg.insn.ea, 'C', &targets) || targets.size() != 1 )
    return MERR_BADARCH;
  mop_t target;
  target.make_gvar(targets[0]);
  if ( conditional )
  {
    // A TRULY constant br_if condition folds the branch (jnz with
    // a constant-loaded kreg fails inside loop bodies - cdg.emit returns
    // nullptr). The `has_constant` hint flag on wasm_value_t can survive
    // stack-slot reuse and operation propagation, so ONLY trust the actual
    // microcode operand (mop_n/mop_fn checked by is_constant()).
    uint64 raw_condition = 0;
    if ( condition.op.is_constant(&raw_condition, false) )
    {
      if ( raw_condition == 0 )
      {
        // never taken: fall through, drop the branch entirely
        return MERR_OK;
      }
      // always taken: unconditional goto
      if ( !branch_types.empty() )
      {
        for ( size_t i = 0; i < branch_types.size(); ++i )
        {
          const wasm_value_t &value = stack[stack.size() - branch_types.size() + i];
          if ( !emit_move(cdg, value.op, target_frame.label_values[i].op,
                          value.type == 0x7D || value.type == 0x7C) )
            return MERR_INTERR;
        }
      }
      if ( cdg.emit(m_goto, &target, nullptr, nullptr) == nullptr )
        return MERR_INTERR;
      stack = target_frame.base_stack;
      unreachable = true;
      add_branch_resume(cdg.insn.ea, targets[0], target_frame, &resume_states);
      return MERR_OK;
    }
    ea_t resume_target = targets[0];
    if ( !resolve_branch_resume_target(targets[0], !branch_types.empty(), &resume_target) )
      return MERR_BADARCH;
    if ( !branch_types.empty() )
    {
      wasm_branch_edge_t edge;
      edge.source = cdg.insn.ea;
      for ( size_t i = 0; i < branch_types.size(); ++i )
      {
        wasm_value_t &value = stack[stack.size() - branch_types.size() + i];
        wasm_branch_value_edge_t &copy = edge.values.push_back();
        if ( !make_operand_register(cdg, value.type, stack.size() - branch_types.size() + i,
                                    &copy.spill)
             || !emit_move(cdg, value.op, copy.spill, value.type == 0x7D || value.type == 0x7C) )
          return MERR_INTERR;
        value.op = copy.spill;
        copy.merge = target_frame.label_values[i].op;
        copy.type = value.type;
        copy.floating = value.type == 0x7D || value.type == 0x7C;
      }
      // The retired end-of-body 0xF2 stub ran these merge moves in
      // its own taken block. Running them here — right before the jnz —
      // is equivalent: the spill registers hold the taken-time values,
      // the taken path jumps straight to the target, and the fallthrough
      // merely sees dead stores it overwrites later. The unconditional
      // lowering below has always moved the payload exactly this way.
      for ( const wasm_branch_value_edge_t &copy : edge.values )
        if ( !emit_move(cdg, copy.spill, copy.merge, copy.floating) )
          return MERR_INTERR;
    }
    mop_t zero;
    zero.make_number(0, 4, cdg.insn.ea, 0);
    if ( cdg.emit(m_jnz, &condition.op, &zero, &target) == nullptr )
      return MERR_INTERR;
    add_branch_resume(cdg.insn.ea, resume_target, target_frame, &resume_states);
    return MERR_OK;
  }
  if ( target_frame.kind == 0x00 )
    return emit_return(cdg);
  for ( size_t i = 0; i < branch_types.size(); ++i )
  {
    const wasm_value_t &value = stack[stack.size() - branch_types.size() + i];
    if ( !emit_move(cdg, value.op, target_frame.label_values[i].op,
                    value.type == 0x7D || value.type == 0x7C) )
      return MERR_INTERR;
  }
  if ( cdg.emit(m_goto, &target, nullptr, nullptr) == nullptr )
    return MERR_INTERR;
  stack = target_frame.base_stack;
  unreachable = true;
  add_branch_resume(cdg.insn.ea, targets[0], target_frame, &resume_states);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_table_size(codegen_t &cdg)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  uint64 minimum = 0;
  uchar flags = 0;
  const auto status = cdg.insn.Op1.type == o_imm
    ? get_wasm_table_metadata(wasm_node, cdg.insn.Op1.value, nullptr, &minimum, &flags)
    : wasm_table_metadata_status_t::malformed;
  if ( status == wasm_table_metadata_status_t::out_of_range )
  {
    msg("WASMHEX_F5_UNSUPPORTED: table.size references an out-of-range table index at %a\n",
        cdg.insn.ea);
    return MERR_BADARCH;
  }
  if ( status != wasm_table_metadata_status_t::ok || (flags & 1) == 0 || (flags & 4) == 0 )
  {
    msg("WASMHEX_F5_UNSUPPORTED: table.size requires declared table min equal to max at %a\n",
        cdg.insn.ea);
    return MERR_BADARCH;
  }
  const bool table64 = (flags & 2) != 0;
  if ( !table64 && minimum > UINT32_MAX )
  {
    msg("WASMHEX_F5_UNSUPPORTED: table.size has an invalid table32 extent at %a\n", cdg.insn.ea);
    return MERR_BADARCH;
  }
  const int size = table64 ? 8 : 4;
  const uchar type = table64 ? 0x7E : 0x7F;
  mop_t result;
  if ( !make_operand_register(cdg, type, stack.size(), &result) )
    return MERR_MEM;
  mop_t constant;
  constant.make_number(minimum, size, cdg.insn.ea, 0);
  if ( !emit_move(cdg, constant, result, false) )
    return MERR_INTERR;
  push(result, type);
  return MERR_OK;
}

merror_t wasmhex_filter_t::emit_data_drop(codegen_t &cdg)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  uint32 data_count = 0;
  if ( wasm_node == BADNODE
       || wasm_node.supval(0, &data_count, sizeof(data_count), 'd') != sizeof(data_count)
       || cdg.insn.Op1.value > UINT32_MAX || cdg.insn.Op1.value >= data_count )
    return MERR_BADARCH;
  const uchar no_argument_types[] = { 0x7F };
  qstring helper;
  helper.sprnt("wasm_data_drop_%u", uint32(cdg.insn.Op1.value));
  return emit_call(cdg, helper.c_str(), 0, 0x40, false, no_argument_types);
}

merror_t wasmhex_filter_t::emit_unreachable(codegen_t &cdg)
{
  if ( controls.empty() || stack.size() < controls.back().base_stack.size() )
    return MERR_BADARCH;
  const uchar no_argument_types[] = { 0x7F };
  // Render as the compiler built-in (GCC/clang __builtin_trap)
  // instead of a wasmhex_* helper - same noreturn semantics.
  const merror_t error
    = emit_call(cdg, "__builtin_trap", 0, 0x40, false, no_argument_types, false, true);
  if ( error != MERR_OK )
    return error;
  stack.resize(controls.back().base_stack.size());
  unreachable = true;
  return MERR_OK;
}
}
