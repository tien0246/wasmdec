#include "module.hpp"

// The processor consumes the ONE decoder (core/binary/
// instruction_stream) for the immediate shapes — itype, imm[0]/imm[1] and
// the instruction size all come from the walker (the shared-table row
// index equals wasmhex_itype_t by construction). What remains below is the
// IDB glue: value-type bounds checks, the 'Y' call.unwind swap, netnode
// memory-mode/alignment checks, table.size metadata, atomic shapes, and
// the loud refusal of retired synthetics.
#include "../../core/binary/instruction_stream.hpp"

namespace wasmhex
{

namespace
{
// Snapshot from EA towards the function end (the largest real instruction
// is a 29,667-byte Go dispatcher br_table; the cap only guards runaway
// input outside any function).
size_t snapshot(ea_t ea, uchar *out, size_t cap)
{
  func_entry_info_t func;
  const bool has_func = get_func_entry_info(&func, ea);
  const ea_t hard = ea + asize_t(cap);
  const ea_t limit = has_func && func.end_ea > ea && func.end_ea < hard ? func.end_ea : hard;
  size_t want = size_t(limit - ea);
  // During load, decode runs BEFORE add_func — no function bounds exist
  // yet, and the 64KB window can overrun the .text end. Shrink until the
  // read succeeds (the walker fails closed on truncation anyway).
  while ( want > 0 && get_bytes(out, want, ea) != want )
    want /= 2;
  return want;
}

// Local reader over the snapshot for trailing immediates the shared table
// does not model (multi-memory indices, 0xFC data/table indices, 0xFD/0xFE
// memargs with their extra validation fields).
struct tail_t
{
  const uchar *data;
  size_t size;
  size_t pos = 0;

  bool uleb(uint64 *v, unsigned bits = 64)
  {
    uint64 value = 0;
    unsigned shift = 0;
    const unsigned max_bytes = (bits + 6) / 7;
    for ( unsigned i = 0; i < max_bytes && pos < size; ++i )
    {
      const uchar b = data[pos++];
      value |= uint64(b & 0x7F) << shift;
      if ( (b & 0x80) == 0 )
      {
        *v = value;
        return true;
      }
      shift += 7;
    }
    return false;
  }
};

bool memory_mode_of(uchar *memory_mode)
{
  netnode wasm_node(wasmhex_node_name, 0, false);
  return wasm_node != BADNODE
    && wasm_node.supval(0, memory_mode, sizeof(*memory_mode), 'M') == sizeof(*memory_mode)
    && (*memory_mode == 1 || *memory_mode == 2);
}

// 0x28..0x3E memarg validation over the snapshot (the walker consumed the
// opcode; the tail starts at the align flags).
bool memarg_plain(const uchar *bytes, size_t avail, uchar opcode, size_t tail_start,
                  uint64 *offset_out)
{
  static constexpr uchar max_alignment[] = {
    2, 3, 2, 3, 0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 2, 3, 2, 3, 0, 1, 0, 1, 2,
  };
  uchar memory_mode = 0;
  if ( !memory_mode_of(&memory_mode) )
    return false;
  tail_t tail{ bytes, avail, tail_start };
  uint64 flags = 0;
  if ( !tail.uleb(&flags, 33) || (flags & ~uint64(0x7F)) != 0
       || (flags & 0x3F) > max_alignment[opcode - 0x28] )
  {
    return false;
  }
  if ( (flags & 0x40) != 0 )
  {
    uint64 memory_index = 0;
    if ( !tail.uleb(&memory_index) || memory_index != 0 )
      return false;
  }
  uint64 offset = 0;
  if ( !tail.uleb(&offset, memory_mode == 2 ? 64 : 32) )
    return false;
  *offset_out = offset;
  return true;
}

void set_op1_dword(insn_t *insn, uint64 value)
{
  insn->Op1.type = o_imm;
  insn->Op1.dtype = dt_dword;
  insn->Op1.value = value;
}
} // namespace

ssize_t wasmhex_procmod_t::decode_instruction(va_list va)
{
  insn_t *insn = va_arg(va, insn_t *);
  uchar buffer[65536];
  // Two-phase snapshot: a small window covers ~99.99% of instructions
  // (median wasm instruction is a few bytes; only giant Go br_table
  // dispatchers exceed 128). get_bytes dominates the decode cost — a
  // full-window copy per instruction made duckdb auto-analysis ~10x
  // slower than the old byte-at-a-time decoder.
  size_t avail = snapshot(insn->ea, buffer, 128);
  if ( avail == 0 )
    return 0;

  loader::binary::insn_cursor_t cursor{ buffer, avail, 0, nullptr };
  loader::binary::decoded_insn_t decoded;
  bool walked = loader::binary::next_insn(cursor, &decoded);
  // Retry once with the full window whenever the small one could not
  // finish the instruction (a 127-target br_table already exceeds 128
  // bytes; the walker reports truncation as "malformed ... depth").
  // Retrying a genuinely malformed instruction just fails identically
  // — deterministic walker, bigger window, same verdict.
  if ( !walked && avail == 128 )
  {
    avail = snapshot(insn->ea, buffer, sizeof(buffer));
    if ( avail > 128 )
    {
      cursor = loader::binary::insn_cursor_t{ buffer, avail, 0, nullptr };
      walked = loader::binary::next_insn(cursor, &decoded);
    }
  }
  if ( !walked )
  {
    static int reported = 0;
    if ( reported++ < 8 )
      msg("M3B2: instruction walker failed at %a (%s)\n", insn->ea,
          cursor.error != nullptr ? cursor.error : "?");
    return 0; // fail-closed: unknown/malformed/truncated
  }

  insn->itype = decoded.itype;
  insn->size = 1 + int(decoded.imm_bytes);
  const uint64 imm0 = decoded.imm[0];
  const uint64 imm1 = decoded.imm[1];
  const uchar opcode = decoded.opcode;
  // tail starts right after the opcode byte (the walker advanced past it)
  const size_t tail_start = 1;

  // Retired synthetic opcodes: identical loud refusal (stale DB guard).
  if ( (opcode >= 0xF0 && opcode <= 0xF7 && opcode != 0xF8) || opcode == 0xF9 || opcode == 0xFA
       || opcode == 0xFF )
  {
    static bool logged_retired_opcode = false;
    if ( !logged_retired_opcode )
    {
      logged_retired_opcode = true;
      msg("WASMHEX_F5_UNSUPPORTED: retired synthetic opcode 0x%02X at %a "
          "(stale database or corrupt .text - reload the .wasm)\n",
          unsigned(opcode), insn->ea);
    }
    return 0;
  }
  if ( opcode == 0xF8 )
  {
    insn->itype = wasmhex_memory_trap;
    return insn->size;
  }

  switch ( insn->itype )
  {
    // trivial / pure-stack
    // (no immediate — itype already set)

    // one uleb -> Op1
    case wasmhex_local_get:
    case wasmhex_local_set:
    case wasmhex_local_tee:
    case wasmhex_global_get:
    case wasmhex_global_set:
    case wasmhex_br:
    case wasmhex_br_if:
    case wasmhex_br_on_null:
    case wasmhex_br_on_non_null:
    case wasmhex_catch:
    case wasmhex_throw:
    case wasmhex_return_call:
    case wasmhex_call_ref:
    case wasmhex_return_call_ref: set_op1_dword(insn, imm0); return insn->size;

    case wasmhex_ref_null:
    {
      // walker decoded the s33 heap type; canonicalize like the old path
      const int64 heap = int64(imm0);
      if ( heap != -16 && heap != -17 && (heap < -22 || heap > -19) )
        return 0;
      set_op1_dword(insn, heap == -16 ? 0x70 : heap == -17 ? 0x6F : heap == -20 ? 0x72 : 0x71);
      return insn->size;
    }

    case wasmhex_ref_func:
      if ( imm0 >= UINT32_MAX || !is_declared_function(uint32(imm0)) )
        return 0;
      set_op1_dword(insn, imm0);
      return insn->size;

    case wasmhex_call:
    {
      // 'Y' metadata may swap a plain call into the call.unwind row
      netnode wasm_node(wasmhex_node_name, 0, false);
      ea_t unwind_catch = BADADDR;
      if ( wasm_node != BADNODE
           && wasm_node.supval_ea(insn->ea, &unwind_catch, sizeof(unwind_catch), 'Y')
             == sizeof(unwind_catch) )
      {
        insn->itype = wasmhex_call_unwind;
      }
      set_op1_dword(insn, imm0);
      return insn->size;
    }

    case wasmhex_call_indirect:
    case wasmhex_return_call_indirect:
      set_op1_dword(insn, imm0);
      insn->Op2.type = o_imm;
      insn->Op2.dtype = dt_dword;
      insn->Op2.value = imm1;
      return insn->size;

    case wasmhex_br_table: set_op1_dword(insn, imm0); return insn->size;

    // consts
    case wasmhex_i32_const: set_op1_dword(insn, imm0); return insn->size;
    case wasmhex_i64_const:
      insn->Op1.type = o_imm;
      insn->Op1.dtype = dt_qword;
      insn->Op1.value = imm0;
      return insn->size;
    case wasmhex_f32_const:
      insn->Op1.type = o_imm;
      insn->Op1.dtype = dt_float;
      insn->Op1.value = imm0;
      return insn->size;
    case wasmhex_f64_const:
      insn->Op1.type = o_imm;
      insn->Op1.dtype = dt_double;
      insn->Op1.value = imm0;
      return insn->size;

    // block types (value type / type index)
    case wasmhex_block:
    case wasmhex_loop:
    case wasmhex_if:
    {
      // The walker decodes EVERY block type as s33 first — so the value
      // type bytes arrive as their negative sleb values (0x7F -> -1 i32,
      // 0x7E -> -2 i64, 0x7D -> -3 f32, 0x7C -> -4 f64, 0x7B -> -5 v128,
      // 0x70 -> -16 funcref, 0x6F -> -17 externref, 0x6A..0x6D ->
      // -22..-19) and the empty type 0x40 arrives as -64. The old path
      // instead sniffed the raw byte first and consumed these single
      // bytes without sleb; only a positive index went through sleb33.
      const int64 type = int64(imm0);
      if ( type >= 0 )
      {
        // s33 type index: bound-check against the type section
        if ( uint64(type) >= uint64(get_module_type_count()) )
          return 0;
      }
      else if ( type != -1 && type != -2 && type != -3 && type != -4 && type != -5 && type != -16
                && type != -17 && !(type >= -22 && type <= -19) && type != -64 )
      {
        return 0;
      }
      return insn->size;
    }

    case wasmhex_try:
      // old path: the trailing byte must be 0x40 (already inside imm_bytes)
      return insn->size;

    case wasmhex_select_typed:
    {
      // Same s33-first walker note as the block types: the valtype byte
      // arrives as its negative sleb value (0x7F -> -1 i32, 0x7E -> -2
      // i64, 0x7D -> -3 f32, 0x7C -> -4 f64, 0x70 -> -16 funcref,
      // 0x6F -> -17 externref, 0x71 -> -21 exnref, 0x72 -> -20 anyref);
      // the old path read the raw byte (select carries no empty form).
      const int64 type = int64(imm0);
      if ( type >= 0 )
      {
        if ( uint64(type) >= uint64(get_module_type_count()) )
          return 0;
      }
      else
      {
        const uchar canonical = uchar(type == -1      ? 0x7F
                                        : type == -2  ? 0x7E
                                        : type == -3  ? 0x7D
                                        : type == -4  ? 0x7C
                                        : type == -16 ? 0x70
                                        : type == -17 ? 0x6F
                                        : type == -21 ? 0x71
                                        : type == -20 ? 0x72
                                                      : uchar(0));
        if ( canonical != 0x7F && canonical != 0x7E && canonical != 0x7D && canonical != 0x7C
             && canonical != 0x70 && canonical != 0x6F && canonical != 0x71 && canonical != 0x72 )
        {
          return 0;
        }
        set_op1_dword(insn, canonical);
      }
      return insn->size;
    }

    // memory size/grow (0x3F/0x40: memidx must be 0)
    case wasmhex_memory_size:
    case wasmhex_memory_grow:
      if ( imm0 != 0 )
        return 0;
      return insn->size;

    // plain loads/stores (0x28..0x3E)
    case wasmhex_i32_load:
    case wasmhex_i64_load:
    case wasmhex_f32_load:
    case wasmhex_f64_load:
    case wasmhex_i32_load8_s:
    case wasmhex_i32_load8_u:
    case wasmhex_i32_load16_s:
    case wasmhex_i32_load16_u:
    case wasmhex_i64_load8_s:
    case wasmhex_i64_load8_u:
    case wasmhex_i64_load16_s:
    case wasmhex_i64_load16_u:
    case wasmhex_i64_load32_s:
    case wasmhex_i64_load32_u:
    case wasmhex_i32_store:
    case wasmhex_i64_store:
    case wasmhex_f32_store:
    case wasmhex_f64_store:
    case wasmhex_i32_store8:
    case wasmhex_i32_store16:
    case wasmhex_i64_store8:
    case wasmhex_i64_store16:
    case wasmhex_i64_store32:
    {
      uint64 offset = 0;
      if ( !memarg_plain(buffer, avail, opcode, tail_start, &offset) )
        return 0;
      uchar memory_mode = 0;
      memory_mode_of(&memory_mode);
      insn->Op1.type = o_imm;
      insn->Op1.dtype = memory_mode == 2 ? dt_qword : dt_dword;
      insn->Op1.value = offset;
      return insn->size;
    }

    default: break;
  }

  // ---- prefix families: the walker consumed the sub-uleb; the rest of
  // their immediates and validations are consumer-owned (moved to the
  // walker together with the Go scans)
  if ( opcode == 0xFC )
  {
    // The walker consumed the prefix sub-uleb: the tail (remaining
    // memidx/dataidx immediates) starts AFTER it — pos 1 counts the
    // opcode byte, so skip len(sub-uleb) more (single byte while sub
    // <= 0x7F; the walker caps sub at 0xFF so at most two).
    const uint64 sub0 = decoded.prefix_sub;
    const size_t sub_len = sub0 > 0x7F ? 2 : 1;
    tail_t tail{ buffer, avail, 1 + sub_len };
    uint64 sub = decoded.prefix_sub;
    if ( sub < 8 )
    {
      // trunc_sat family: itype = i32_trunc_sat_f32_s + sub (the table rows
      // already resolve it; Op1 none)
      return insn->size;
    }
    if ( sub == 8 || sub == 9 )
    {
      netnode wasm_node(wasmhex_node_name, 0, false);
      uint32 data_count = 0;
      uint64 data_index = 0, memory_index = 0;
      if ( wasm_node == BADNODE
           || wasm_node.supval(0, &data_count, sizeof(data_count), 'd') != sizeof(data_count)
           || !tail.uleb(&data_index) || data_index >= data_count )
      {
        return 0;
      }
      if ( sub == 8 )
      {
        uchar memory_mode = 0;
        if ( !memory_mode_of(&memory_mode) || !tail.uleb(&memory_index) || memory_index != 0 )
          return 0;
        insn->itype = wasmhex_memory_init;
        insn->Op2.type = o_imm;
        insn->Op2.dtype = dt_dword;
        insn->Op2.value = memory_index;
      }
      else
      {
        insn->itype = wasmhex_data_drop;
      }
      set_op1_dword(insn, data_index);
      insn->size = int(tail.pos); // tail.pos = absolute offset of the next byte
      return insn->size;
    }
    if ( sub == 16 )
    {
      netnode wasm_node(wasmhex_node_name, 0, false);
      uint64 table_index = 0;
      if ( !tail.uleb(&table_index) )
      {
        msg("WASMHEX_TABLE_SIZE_REJECTED: malformed table index at %a\n", insn->ea);
        return 0;
      }
      uint64 minimum = 0;
      uchar table_flags = 0;
      const auto status
        = get_wasm_table_metadata(wasm_node, table_index, nullptr, &minimum, &table_flags);
      if ( status == wasm_table_metadata_status_t::out_of_range )
      {
        msg("WASMHEX_TABLE_SIZE_REJECTED: table index %llu is out of range at %a\n", table_index,
            insn->ea);
        return 0;
      }
      if ( status != wasm_table_metadata_status_t::ok )
      {
        msg("WASMHEX_TABLE_SIZE_REJECTED: table metadata is missing or malformed at %a\n",
            insn->ea);
        return 0;
      }
      if ( (table_flags & 1) == 0 || (table_flags & 4) == 0 )
      {
        msg("WASMHEX_TABLE_SIZE_REJECTED: table.size requires declared table min equal to max "
            "at %a\n",
            insn->ea);
        return 0;
      }
      insn->itype = wasmhex_table_size;
      insn->Op1.type = o_imm;
      insn->Op1.dtype = (table_flags & 2) != 0 ? dt_qword : dt_dword;
      insn->Op1.value = table_index;
      insn->size = int(tail.pos);
      return insn->size;
    }
    if ( sub != 10 && sub != 11 )
      return 0;
    {
      uchar memory_mode = 0;
      if ( !memory_mode_of(&memory_mode) )
        return 0;
      uint64 destination_memory = 0, source_memory = 0;
      if ( !tail.uleb(&destination_memory) || destination_memory != 0
           || (sub == 10 && (!tail.uleb(&source_memory) || source_memory != 0)) )
      {
        return 0;
      }
      insn->itype = sub == 10 ? wasmhex_memory_copy : wasmhex_memory_fill;
      insn->size = int(tail.pos);
      return insn->size;
    }
  }
  if ( opcode == 0xFD )
  {
    // Same sub-uleb note as the 0xFC family: the walker consumed it.
    const uint64 sub0 = decoded.prefix_sub;
    const size_t sub_len = sub0 > 0x7F ? 2 : 1;
    tail_t tail{ buffer, avail, 1 + sub_len };
    const uint64 sub = decoded.prefix_sub;
    const uint64 access_width = sub == 0 || sub == 11    ? 16
      : sub >= 1 && sub <= 6                             ? 8
      : sub == 7 || sub == 84 || sub == 88               ? 1
      : sub == 8 || sub == 85 || sub == 89               ? 2
      : sub == 9 || sub == 92 || sub == 86 || sub == 90  ? 4
      : sub == 10 || sub == 93 || sub == 87 || sub == 91 ? 8
                                                         : 0;
    if ( access_width == 0 )
      return 0;
    uint64 max_alignment = 0;
    for ( uint64 width = access_width; width > 1; width >>= 1 )
      ++max_alignment;
    uchar memory_mode = 0;
    if ( !memory_mode_of(&memory_mode) )
      return 0;
    uint64 flags = 0, memory_index = 0;
    if ( !tail.uleb(&flags, 33) || (flags & ~uint64(0x7F)) != 0 || (flags & 0x3F) > max_alignment
         || ((flags & 0x40) != 0 && (!tail.uleb(&memory_index) || memory_index != 0)) )
    {
      return 0;
    }
    uint64 offset = 0;
    if ( !tail.uleb(&offset, memory_mode == 2 ? 64 : 32) )
      return 0;
    const bool lane_operation = sub >= 84 && sub <= 91;
    uint64 lane = 0;
    if ( lane_operation )
    {
      if ( tail.pos >= avail )
        return 0;
      lane = buffer[tail.pos++];
      if ( lane >= 16 / access_width )
        return 0;
    }
    insn->Op1.type = o_imm;
    insn->Op1.dtype = memory_mode == 2 ? dt_qword : dt_dword;
    insn->Op1.value = offset;
    if ( lane_operation )
    {
      insn->Op2.type = o_imm;
      insn->Op2.dtype = dt_byte;
      insn->Op2.value = lane;
    }
    insn->size = int(tail.pos);
    return insn->size;
  }
  if ( opcode == 0xFE )
  {
    // Same sub-uleb note as the 0xFC family: the walker consumed it.
    const uint64 sub0 = decoded.prefix_sub;
    const size_t sub_len = sub0 > 0x7F ? 2 : 1;
    tail_t tail{ buffer, avail, 1 + sub_len };
    uint64 sub = decoded.prefix_sub;
    wasmhex_atomic_info_t atomic;
    if ( !wasmhex_atomic_info(sub, &atomic) )
      return 0;
    insn->itype = wasmhex_atomic;
    set_op1_dword(insn, sub);
    if ( atomic.kind == wasmhex_atomic_kind_t::fence )
    {
      if ( tail.pos >= avail || buffer[tail.pos++] != 0 )
        return 0;
      insn->size = int(tail.pos);
      return insn->size;
    }
    uchar memory_mode = 0;
    if ( !memory_mode_of(&memory_mode) )
      return 0;
    const unsigned expected_alignment = atomic.access_size == 8 ? 3
      : atomic.access_size == 4                                 ? 2
      : atomic.access_size == 2                                 ? 1
                                                                : 0;
    uint64 flags = 0, memory_index = 0, offset = 0;
    if ( !tail.uleb(&flags, 33) || (flags & ~uint64(0x7F)) != 0
         || (flags & 0x3F) != expected_alignment
         || ((flags & 0x40) != 0 && (!tail.uleb(&memory_index) || memory_index != 0))
         || !tail.uleb(&offset, memory_mode == 2 ? 64 : 32) )
    {
      return 0;
    }
    insn->Op2.type = o_imm;
    insn->Op2.dtype = dt_byte;
    insn->Op2.value = expected_alignment;
    insn->Op3.type = o_imm;
    insn->Op3.dtype = memory_mode == 2 ? dt_qword : dt_dword;
    insn->Op3.value = offset;
    insn->size = int(tail.pos);
    return insn->size;
  }
  if ( opcode == 0xFB )
    return insn->size; // i31 family: sub only

  // everything else: itype set, no operands
  return insn->size;
}

} // namespace wasmhex
