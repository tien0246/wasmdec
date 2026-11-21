// The instruction-stream walker — every IMM kind + fail-closed
// malformed cases. The corpus cross-check (python mirror vs processor on
// duckdb/main/gowasm: 386k instructions identical) lives in the round
// report; these unit tests pin the shapes locally.
#include "doctest.h"

#include <cstring>
#include <vector>

#include "../core/binary/instruction_stream.hpp"

using namespace wasmhex::loader;
using namespace wasmhex::loader::binary;

namespace
{
std::vector<std::uint8_t> bytes(std::initializer_list<std::uint8_t> list)
{
  return std::vector<std::uint8_t>(list);
}

decoded_insn_t walk_ok(const std::vector<std::uint8_t> &buf)
{
  insn_cursor_t cursor{ buf.data(), buf.size(), 0, nullptr };
  decoded_insn_t insn;
  REQUIRE(next_insn(cursor, &insn));
  return insn;
}

bool walk_fails(const std::vector<std::uint8_t> &buf)
{
  insn_cursor_t cursor{ buf.data(), buf.size(), 0, nullptr };
  decoded_insn_t insn;
  return !next_insn(cursor, &insn) && cursor.error != nullptr;
}
} // namespace

TEST_CASE("G1 walker: no-immediate opcodes")
{
  const auto insn = walk_ok(bytes({ 0x01 })); // nop
  CHECK(insn.opcode == 0x01);
  CHECK(insn.imm_bytes == 0);
  CHECK(walk_ok(bytes({ 0x00 })).opcode == 0x00); // unreachable
  CHECK(walk_ok(bytes({ 0x1A })).opcode == 0x1A); // drop
  CHECK(walk_ok(bytes({ 0x1B })).opcode == 0x1B); // select
}

TEST_CASE("G1 walker: uleb, const, memarg shapes")
{
  const auto local = walk_ok(bytes({ 0x20, 0x7F })); // local.get 127
  CHECK(local.imm[0] == 127);
  CHECK(local.imm_bytes == 1);

  const auto i32c = walk_ok(bytes({ 0x41, 0x7F })); // i32.const -1
  CHECK(static_cast<std::int64_t>(i32c.imm[0]) == -1);

  const auto i64c = walk_ok(bytes({ 0x42, 0x80, 0x80, 0x80, 0x80, 0x80, 0x01 }));
  CHECK(i64c.imm[0] == std::uint64_t(std::int64_t(1) << 35)); // 6-byte sleb

  // memarg: 2-byte align + 2-byte offset (full-length LEBs — the loader
  // scan's 1-byte assumption was the bug this walker fixes)
  const auto memarg = walk_ok(bytes({ 0x28, 0x82, 0x00, 0x81, 0x00 }));
  CHECK(memarg.imm[0] == 2);
  CHECK(memarg.imm[1] == 1);
  CHECK(memarg.imm_bytes == 4);

  CHECK(walk_ok(bytes({ 0x43, 1, 2, 3, 4 })).imm[0] == 0x04030201); // f32.const
  CHECK(walk_ok(bytes({ 0x44, 1, 2, 3, 4, 5, 6, 7, 8 })).imm_bytes == 8);
}

TEST_CASE("G1 walker: br_table, call_indirect, blocktype, select_typed")
{
  const auto brt = walk_ok(bytes({ 0x0E, 0x02, 0x00, 0x01, 0x00 }));
  CHECK(brt.imm[0] == 2);
  CHECK(brt.imm_bytes == 4);

  const auto ci = walk_ok(bytes({ 0x11, 0x03, 0x00 }));
  CHECK(ci.imm[0] == 3);
  CHECK(ci.imm[1] == 0);

  const auto block = walk_ok(bytes({ 0x02, 0x7F }));
  CHECK(static_cast<std::int64_t>(block.imm[0]) == -1);  // sleb33 view of 0x7F
  const auto block_idx = walk_ok(bytes({ 0x02, 0x7B })); // s33 type index
  CHECK(block_idx.imm_bytes == 1);

  const auto sel = walk_ok(bytes({ 0x1C, 0x01, 0x7F }));
  CHECK(static_cast<std::int64_t>(sel.imm[0]) == -1); // sleb33 view
}

TEST_CASE("G1 walker: prefix sub-opcodes, lanes, shuffle, atomics")
{
  // i32.add (plain 0x6A) next to v128 lane op (0xFD 84 + lane byte)
  const auto lane = walk_ok(bytes({ 0xFD, 84, 0x02 }));
  CHECK(lane.opcode == 0xFD);
  CHECK(lane.prefix_sub == 84);
  CHECK(lane.imm_bytes == 2); // sub-uleb + lane byte

  // v128.shuffle (sub 12) and v128.const (sub 12 legacy) have no table
  // row — the processor rejects them as well; the walker fails closed
  CHECK(walk_fails({ 0xFD, 12, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }));

  const auto atomic = walk_ok(bytes({ 0xFE, 0x0E }));
  CHECK(atomic.opcode == 0xFE);

  // 0xFC rows own ONLY the sub-uleb in the walker (IMM_FC_SUB); the
  // trailing memory.init/copy/fill ulebs are consumer-owned (those scans
  // moved here)
  const auto fc = walk_ok(bytes({ 0xFC, 0x0B })); // memory.fill
  CHECK(fc.prefix_sub == 0x0B);
  CHECK(fc.imm_bytes == 1);

  CHECK(walk_ok(bytes({ 0xFB, 28 })).prefix_sub == 28); // ref.i31
}

TEST_CASE("G1 walker: fail-closed on malformed input")
{
  CHECK(walk_fails({}));                    // end of stream
  CHECK(walk_fails({ 0x20 }));              // truncated uleb
  CHECK(walk_fails({ 0x41 }));              // truncated sleb
  CHECK(walk_fails({ 0x43, 1, 2, 3 }));     // truncated f32
  CHECK(walk_fails({ 0x28, 0x00 }));        // truncated memarg
  CHECK(walk_fails({ 0x0E, 0x02, 0x00 }));  // truncated depths
  CHECK(walk_fails({ 0xFD, 84 }));          // lane without byte
  CHECK(walk_fails({ 0xFD, 12, 1, 2, 3 })); // short shuffle
  CHECK(walk_fails({ 0xC5 }));              // unknown opcode
  CHECK(walk_fails({ 0xF0 }));              // retired synthetic
  CHECK(walk_fails({ 0xFF }));              // retired goto
}
