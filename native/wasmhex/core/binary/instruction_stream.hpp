#pragma once

#include <cstddef>
#include <cstdint>

#include "instructions.hpp"
#include "leb128.hpp"
#include "reader.hpp"

namespace wasmhex::loader::binary
{

struct decoded_insn_t
{
  std::uint32_t offset = 0;    // from the cursor base (instruction START)
  std::uint8_t opcode = 0;     // first byte (raw)
  std::uint8_t prefix_sub = 0; // 0xFC/0xFD/0xFB/0xFE sub-opcode, else 0
  int itype = 0;               // WASMHEX_ITYPES row index (see header note)
  std::uint64_t imm[2] = { 0, 0 };
  std::uint32_t imm_bytes = 0; // immediate bytes consumed (size = 1 + this)
};

struct insn_cursor_t
{
  const std::uint8_t *data = nullptr;
  std::size_t size = 0;
  std::size_t pos = 0;
  // Fail-closed diagnostic (stable prefix "instruction stream:"), set by
  // next_insn() when it returns false.
  const char *error = nullptr;

  std::size_t consumed() const { return pos; }
};

// Decodes the instruction at cursor.pos. Returns false (and sets
// cursor.error) on: end of stream, an unknown opcode/sub-opcode, or a
// malformed immediate. The decoded instruction lands in *out.
bool next_insn(insn_cursor_t &cursor, decoded_insn_t *out);

} // namespace wasmhex::loader::binary
