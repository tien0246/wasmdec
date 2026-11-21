// The single instruction decoder — implementation. See the header
// for the contract; every immediate shape comes from the shared
// WASMHEX_ITYPES table, never from a hand-written switch.
#include "instruction_stream.hpp"

#include <cstring>

namespace wasmhex::loader::binary
{

namespace
{

using namespace wasmhex::opcodes;
using opcodes::imm_kind_t;

// Row metadata generated once from the shared table: (byte, sub, imm kind,
// row index). `sub` is WASMHEX_NO_SUB for plain opcodes.
struct row_meta_t
{
  int byte;
  int sub;
  int imm;
  int index;
};

enum wasmhex_meta_index_t
{
#define WASMHEX_GEN_INDEX(name, byte, sub, imm, mnem, flags) wasmhex_meta_##name,
  WASMHEX_ITYPES(WASMHEX_GEN_INDEX)
#undef WASMHEX_GEN_INDEX
};

constexpr row_meta_t k_rows[] = {
#define WASMHEX_GEN_ROW(name, byte, sub, imm, mnem, flags)                                         \
  { byte, sub, int(imm), wasmhex_meta_##name },
  WASMHEX_ITYPES(WASMHEX_GEN_ROW)
#undef WASMHEX_GEN_ROW
};
constexpr int k_row_count = int(sizeof(k_rows) / sizeof(k_rows[0]));

constexpr int k_prefixes[] = { 0xFB, 0xFC, 0xFD, 0xFE };

bool is_prefix(int byte)
{
  for ( int p : k_prefixes )
    if ( p == byte )
      return true;
  return false;
}

// Plain-opcode lookup: byte == row.byte && row.sub == NO_SUB. The null
// sentinel (byte 0 without sub) and the synthetic retired rows (0xF0-0xF7,
// 0xF9, 0xFA, 0xFF — they have entries only to keep itype numbers stable)
// stay UNDECODABLE here: meeting one in a code stream is a hard error.
const row_meta_t *find_plain(int byte, int *row_count_out)
{
  const row_meta_t *found = nullptr;
  int hits = 0;
  for ( const row_meta_t &row : k_rows )
  {
    if ( row.byte != byte || row.sub != opcodes::WASMHEX_NO_SUB )
      continue;
    // skip the null sentinel (byte 0) and retired synthetic encodings
    if ( row.index == wasmhex_meta_null )
      continue;
    if ( byte >= 0xF0 && byte <= 0xFF && byte != 0xF8 )
      return nullptr; // retired synthetics — never decode
    // first row wins (same as the processor's opcodes_lookup): 0x10 has
    // two rows — call and its call_unwind alias; the loader swaps to the
    // alias via 'Y' metadata, the walker decodes the plain call
    if ( found == nullptr )
      found = &row;
    ++hits;
  }
  if ( row_count_out != nullptr )
    *row_count_out = hits;
  return found;
}

// Prefix lookup: byte == row.byte && row.sub == sub.
const row_meta_t *find_prefixed(int byte, int sub)
{
  for ( const row_meta_t &row : k_rows )
    if ( row.byte == byte && row.sub == sub )
      return &row;
  return nullptr;
}

} // namespace

bool next_insn(insn_cursor_t &cursor, decoded_insn_t *out)
{
  cursor.error = nullptr;
  if ( out == nullptr || cursor.data == nullptr )
  {
    cursor.error = "instruction stream: bad cursor";
    return false;
  }
  if ( cursor.pos >= cursor.size )
  {
    cursor.error = "instruction stream: end of stream";
    return false;
  }

  reader_t r = { cursor.data, cursor.size, cursor.pos };
  const std::size_t start = cursor.pos;
  std::uint8_t opcode = 0;
  if ( !r.byte(&opcode) )
  {
    cursor.error = "instruction stream: truncated opcode";
    return false;
  }

  int itype = -1;
  imm_kind_t kind = opcodes::IMM_NONE;
  std::uint8_t prefix_sub = 0;

  if ( is_prefix(opcode) )
  {
    // 0xFE (atomic) keeps a single table row (sub NO_SUB) — the walker
    // records the sub-uleb but the table has no per-sub rows for it.
    std::uint64_t sub = 0;
    if ( !r.uleb(&sub) || sub > 0xFF )
    {
      cursor.error = "instruction stream: malformed prefix sub-opcode";
      return false;
    }
    prefix_sub = std::uint8_t(sub);
    const row_meta_t *row = find_prefixed(opcode, prefix_sub);
    if ( row == nullptr )
    {
      // 0xFE: all sub-opcodes share the `atomic` row; anything else is
      // unknown
      if ( opcode != 0xFE )
      {
        cursor.error = "instruction stream: unknown prefix sub-opcode";
        return false;
      }
      row = find_prefixed(opcode, opcodes::WASMHEX_NO_SUB);
      if ( row == nullptr )
      {
        cursor.error = "instruction stream: unknown prefix opcode";
        return false;
      }
    }
    itype = row->index;
    kind = imm_kind_t(row->imm);
    // the sub-uleb is part of the "immediate" for shape purposes
    switch ( kind )
    {
      case opcodes::IMM_FD_SUB:
      case opcodes::IMM_FB_SUB:
      case opcodes::IMM_FC_SUB:
      case opcodes::IMM_FE_SUB:
        kind = opcodes::IMM_NONE; // sub consumed above; lanes handled below
        break;
      default: break;
    }
  }
  else
  {
    int hits = 0;
    const row_meta_t *row = find_plain(opcode, &hits);
    if ( row == nullptr )
    {
      cursor.error = hits > 1 ? "instruction stream: ambiguous opcode row"
                              : "instruction stream: unknown opcode";
      return false;
    }
    itype = row->index;
    kind = imm_kind_t(row->imm);
  }

  decoded_insn_t insn;
  insn.offset = std::uint32_t(start);
  insn.opcode = opcode;
  insn.prefix_sub = prefix_sub;
  insn.itype = itype;

  std::uint64_t imm0 = 0, imm1 = 0;
  switch ( kind )
  {
    case opcodes::IMM_NONE: break;
    case opcodes::IMM_ULEB:
    case opcodes::IMM_CALL:
    case opcodes::IMM_CALL_UNWIND:
      if ( !r.uleb(&imm0) )
      {
        cursor.error = "instruction stream: malformed uleb immediate";
        return false;
      }
      break;
    case opcodes::IMM_CALL_INDIRECT:
      if ( !r.uleb(&imm0) || !r.uleb(&imm1) )
      {
        cursor.error = "instruction stream: malformed call_indirect immediates";
        return false;
      }
      break;
    case opcodes::IMM_BR_TABLE:
    {
      std::uint64_t count = 0;
      if ( !r.uleb(&count) || count > 0xFFFFF )
      {
        cursor.error = "instruction stream: malformed br_table count";
        return false;
      }
      imm0 = count;
      for ( std::uint64_t i = 0; i <= count; ++i )
      {
        std::uint64_t depth = 0;
        if ( !r.uleb(&depth) )
        {
          cursor.error = "instruction stream: malformed br_table depth";
          return false;
        }
      }
      break;
    }
    case opcodes::IMM_BLOCKTYPE:
    {
      std::int64_t s33 = 0;
      const std::size_t before = r.pos;
      if ( r.sleb33(&s33) )
        imm0 = std::uint64_t(s33);
      else
      {
        r.pos = before;
        // not a (signed) type index: must be one value-type byte
        if ( r.pos >= r.size )
        {
          cursor.error = "instruction stream: truncated block type";
          return false;
        }
        imm0 = r.data[r.pos];
        r.pos += 1;
      }
      break;
    }
    case opcodes::IMM_I32_CONST:
    {
      // sleb32 == 33 payload bits with sign extension — sleb33 covers it
      std::int64_t value = 0;
      if ( !r.sleb33(&value) )
      {
        cursor.error = "instruction stream: malformed i32.const";
        return false;
      }
      imm0 = std::uint64_t(value);
      break;
    }
    case opcodes::IMM_I64_CONST:
    {
      std::int64_t value = 0;
      if ( !r.sleb64(&value) )
      {
        cursor.error = "instruction stream: malformed i64.const";
        return false;
      }
      imm0 = std::uint64_t(value);
      break;
    }
    case opcodes::IMM_F32_CONST:
    {
      if ( !r.fixed(&imm0, 4) )
      {
        cursor.error = "instruction stream: truncated f32.const";
        return false;
      }
      break;
    }
    case opcodes::IMM_F64_CONST:
    {
      if ( !r.fixed(&imm0, 8) )
      {
        cursor.error = "instruction stream: truncated f64.const";
        return false;
      }
      break;
    }
    case opcodes::IMM_MEMARG:
    {
      // full-length LEBs on both fields (the 1-byte assumption was the
      // loader scan bug this walker fixes); bit 6 (0x40) indicates a
      // multi-memory index between align_flags and offset (must be 0).
      std::uint64_t align_flags = 0, offset = 0;
      if ( !r.uleb(&align_flags, 33) || (align_flags & ~std::uint64_t(0x7F)) != 0 )
      {
        cursor.error = "instruction stream: malformed memarg";
        return false;
      }
      if ( (align_flags & 0x40) != 0 )
      {
        std::uint64_t mem_index = 0;
        if ( !r.uleb(&mem_index) || mem_index != 0 )
        {
          cursor.error = "instruction stream: malformed memarg memory index";
          return false;
        }
      }
      if ( !r.uleb(&offset, 64) )
      {
        cursor.error = "instruction stream: malformed memarg";
        return false;
      }
      imm0 = align_flags;
      imm1 = offset;
      break;
    }
    case opcodes::IMM_MEM_SIZEGROW:
      if ( !r.uleb(&imm0) )
      {
        cursor.error = "instruction stream: malformed memory index";
        return false;
      }
      break;
    case opcodes::IMM_REF_NULL:
    {
      std::int64_t heap = 0;
      if ( !r.sleb33(&heap) )
      {
        cursor.error = "instruction stream: malformed heap type";
        return false;
      }
      imm0 = std::uint64_t(heap);
      break;
    }
    case opcodes::IMM_SELECT_TYPED:
    {
      std::uint64_t count = 0;
      if ( !r.uleb(&count) || count != 1 )
      {
        cursor.error = "instruction stream: select_typed must carry one type";
        return false;
      }
      std::int64_t heap = 0;
      const std::size_t before = r.pos;
      if ( r.sleb33(&heap) )
      {
        imm0 = std::uint64_t(heap);
      }
      else
      {
        r.pos = before;
        if ( r.pos >= r.size )
        {
          cursor.error = "instruction stream: truncated select_typed";
          return false;
        }
        imm0 = r.data[r.pos];
        r.pos += 1;
      }
      break;
    }
    case opcodes::IMM_MEMCHECK:
    {
      std::uint64_t check = 0, span = 0;
      if ( !r.uleb(&check) || !r.uleb(&span) )
      {
        cursor.error = "instruction stream: malformed memory check";
        return false;
      }
      imm0 = check;
      imm1 = span;
      break;
    }
    default: cursor.error = "instruction stream: retired or special immediate kind"; return false;
  }

  // SIMD lane/shuffle immediates follow specific 0xFD sub-opcodes: 84..91
  // (lane) and 12 (shuffle). The table rows exist; decode the trailing
  // bytes here (1 lane byte / 16 shuffle bytes).
  if ( opcode == 0xFD )
  {
    const bool lane = prefix_sub >= 84 && prefix_sub <= 91;
    const bool shuffle = prefix_sub == 12;
    if ( lane && !r.skip(1) )
    {
      cursor.error = "instruction stream: truncated lane immediate";
      return false;
    }
    if ( shuffle && !r.skip(16) )
    {
      cursor.error = "instruction stream: truncated shuffle immediate";
      return false;
    }
  }

  insn.imm[0] = imm0;
  insn.imm[1] = imm1;
  insn.imm_bytes = std::uint32_t(r.pos - start - 1);
  cursor.pos = r.pos;
  *out = insn;
  return true;
}

} // namespace wasmhex::loader::binary
