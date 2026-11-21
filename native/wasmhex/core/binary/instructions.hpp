// Single source of truth for wasmhex opcode metadata.
//
// RETIRED SYNTHETIC OPCODES: 0xF0, 0xF1, 0xF2, 0xF3-0xF7,
// 0xF9, 0xFA and 0xFF are never emitted into .text any more and decode.cpp
// fails loudly if it meets one (stale database or corrupt .text). Their
// X() entries below stay ONLY so the itype enum values remain stable for
// databases written before the retirement — do not decode or lower them.
// 0xF8 (memory_trap) is LIVE: it lowers real statically-known traps.
//
// Every (opcode byte / prefix sub-opcode) -> itype name, mnemonic, immediate
// shape and CF_ flags pair is defined here exactly once. The processor's
// itype enum and instruction table are generated from this table (same
// order, so existing itype numbers — and therefore existing IDBs — are
// preserved), the decoder walks immediates from the IMM_* kinds, and the
// loader's instruction preflight uses the same kinds.
//
// byte/sub semantics:
//   byte           single-byte opcode; WASMHEX_NO_SUB for plain opcodes
//   0xFB/0xFC/0xFD  prefix opcodes whose sub-uleb selects the itype
//   null           itype 0 has no encoding; call_unwind decodes as 0x10
//                  (the loader swaps it to call.unwind via 'Y' metadata)
// Hand-aligned X-macro table; #flags is stringified by tests, so whitespace
// inside it is observable.
// clang-format off
#pragma once
#include <cstdint>

#include <cstddef>  // size_t in lookups

namespace wasmhex::opcodes
{

enum imm_kind_t : std::uint8_t
{
  IMM_NONE,           // no immediate bytes
  IMM_ULEB,           // one uleb32 -> Op1 (dword)
  IMM_CALL,           // call: one uleb + possible 'Y' unwind swap
  IMM_CALL_UNWIND,    // alias row for the same 0x10 encoding
  IMM_CALL_INDIRECT,  // two ulebs -> Op1 (type), Op2 (table)
  IMM_BR_TABLE,       // count + (count+1) depth ulebs
  IMM_BLOCKTYPE,      // value type / s33 type index (block/loop/if)
  IMM_I32_CONST,      // sleb32
  IMM_I64_CONST,      // sleb64
  IMM_F32_CONST,      // 4 raw bytes
  IMM_F64_CONST,      // 8 raw bytes
  IMM_MEMARG,         // alignment flags (+ memory index) + address offset
  IMM_MEM_SIZEGROW,   // memory index uleb (must be 0)
  IMM_REF_NULL,       // s33 heap type, canonicalized
  IMM_SELECT_TYPED,   // typed select vector (validated)
  IMM_MEMCHECK,       // synthesized memory bounds check (0xF9)
  IMM_FC_SUB,         // 0xFC-prefixed sub-opcode
  IMM_FB_SUB,         // 0xFB-prefixed sub-opcode (i31)
  IMM_FD_SUB,         // 0xFD-prefixed SIMD sub-opcode
  IMM_FE_SUB,         // 0xFE-prefixed atomic sub-opcode
  IMM_SPECIAL,        // pseudo rows without a real encoding
};

constexpr int WASMHEX_NO_SUB = -1;

// X(name, byte, sub, imm, mnemonic, insn_flags)
#define WASMHEX_ITYPES(X) \
  X(null, 0, WASMHEX_NO_SUB, IMM_NONE, "", 0) \
  X(unreachable, 0, WASMHEX_NO_SUB, IMM_NONE, "unreachable", CF_STOP) \
  X(nop, 0x01, WASMHEX_NO_SUB, IMM_NONE, "nop", 0) \
  X(drop, 0x1A, WASMHEX_NO_SUB, IMM_NONE, "drop", 0) \
  X(select, 0x1B, WASMHEX_NO_SUB, IMM_NONE, "select", 0) \
  X(select_typed, 0x1C, WASMHEX_NO_SUB, IMM_SELECT_TYPED, "select_typed", 0) \
  X(end, 0x0B, WASMHEX_NO_SUB, IMM_NONE, "end", 0) \
  X(block, 0x02, WASMHEX_NO_SUB, IMM_BLOCKTYPE, "block", 0) \
  X(loop, 0x03, WASMHEX_NO_SUB, IMM_BLOCKTYPE, "loop", 0) \
  X(if, 0x04, WASMHEX_NO_SUB, IMM_BLOCKTYPE, "if", CF_JUMP) \
  X(try, 0x06, WASMHEX_NO_SUB, IMM_SPECIAL, "try", 0) \
  X(catch, 0x07, WASMHEX_NO_SUB, IMM_ULEB, "catch", CF_USE1) \
  X(throw, 0x08, WASMHEX_NO_SUB, IMM_ULEB, "throw", CF_USE1|CF_STOP) \
  X(else, 0x05, WASMHEX_NO_SUB, IMM_NONE, "else", CF_JUMP|CF_STOP) \
  X(br, 0x0C, WASMHEX_NO_SUB, IMM_ULEB, "br", CF_USE1|CF_JUMP|CF_STOP) \
  X(br_if, 0x0D, WASMHEX_NO_SUB, IMM_ULEB, "br_if", CF_USE1|CF_JUMP) \
  X(br_on_null, 0xD5, WASMHEX_NO_SUB, IMM_ULEB, "br_on_null", CF_USE1|CF_JUMP) \
  X(br_on_non_null, 0xD6, WASMHEX_NO_SUB, IMM_ULEB, "br_on_non_null", CF_USE1|CF_JUMP) \
  X(br_table, 0x0E, WASMHEX_NO_SUB, IMM_BR_TABLE, "br_table", CF_USE1|CF_JUMP|CF_STOP) \
  X(br_case, 0xF0, WASMHEX_NO_SUB, IMM_ULEB, "br.case", CF_USE1|CF_JUMP) \
  X(br_default, 0xF1, WASMHEX_NO_SUB, IMM_NONE, "br.default", CF_JUMP|CF_STOP) \
  X(branch_edge, 0xF2, WASMHEX_NO_SUB, IMM_ULEB, "br.edge", CF_USE1|CF_JUMP|CF_STOP) \
  X(call_dispatch_test, 0xF3, WASMHEX_NO_SUB, IMM_ULEB, "call.case", CF_USE1|CF_JUMP) \
  X(call_dispatch_default, 0xF4, WASMHEX_NO_SUB, IMM_NONE, "call.bounds", CF_STOP) \
  X(call_dispatch_target, 0xF5, WASMHEX_NO_SUB, IMM_ULEB, "call.target", CF_CALL|CF_STOP) \
  X(call_dispatch_null, 0xF6, WASMHEX_NO_SUB, IMM_NONE, "call.null", CF_STOP) \
  X(call_dispatch_type, 0xF7, WASMHEX_NO_SUB, IMM_NONE, "call.type", CF_STOP) \
  X(memory_bounds_check, 0xF9, WASMHEX_NO_SUB, IMM_MEMCHECK, "memory.check", CF_USE1|CF_USE2) \
  X(memory_trap, 0xF8, WASMHEX_NO_SUB, IMM_NONE, "memory.trap", CF_STOP) \
  X(return, 0x0F, WASMHEX_NO_SUB, IMM_NONE, "return", CF_STOP) \
  X(local_get, 0x20, WASMHEX_NO_SUB, IMM_ULEB, "local.get", 0) \
  X(local_set, 0x21, WASMHEX_NO_SUB, IMM_ULEB, "local.set", 0) \
  X(local_tee, 0x22, WASMHEX_NO_SUB, IMM_ULEB, "local.tee", 0) \
  X(global_get, 0x23, WASMHEX_NO_SUB, IMM_ULEB, "global.get", 0) \
  X(global_set, 0x24, WASMHEX_NO_SUB, IMM_ULEB, "global.set", 0) \
  X(i32_const, 0x41, WASMHEX_NO_SUB, IMM_I32_CONST, "i32.const", 0) \
  X(i32_clz, 0x67, WASMHEX_NO_SUB, IMM_NONE, "i32.clz", 0) \
  X(i32_ctz, 0x68, WASMHEX_NO_SUB, IMM_NONE, "i32.ctz", 0) \
  X(i32_popcnt, 0x69, WASMHEX_NO_SUB, IMM_NONE, "i32.popcnt", 0) \
  X(i64_clz, 0x79, WASMHEX_NO_SUB, IMM_NONE, "i64.clz", 0) \
  X(i64_ctz, 0x7A, WASMHEX_NO_SUB, IMM_NONE, "i64.ctz", 0) \
  X(i64_popcnt, 0x7B, WASMHEX_NO_SUB, IMM_NONE, "i64.popcnt", 0) \
  X(i32_eqz, 0x45, WASMHEX_NO_SUB, IMM_NONE, "i32.eqz", 0) \
  X(i32_eq, 0x46, WASMHEX_NO_SUB, IMM_NONE, "i32.eq", 0) \
  X(i32_ne, 0x47, WASMHEX_NO_SUB, IMM_NONE, "i32.ne", 0) \
  X(i32_lt_s, 0x48, WASMHEX_NO_SUB, IMM_NONE, "i32.lt_s", 0) \
  X(i32_lt_u, 0x49, WASMHEX_NO_SUB, IMM_NONE, "i32.lt_u", 0) \
  X(i32_gt_s, 0x4A, WASMHEX_NO_SUB, IMM_NONE, "i32.gt_s", 0) \
  X(i32_gt_u, 0x4B, WASMHEX_NO_SUB, IMM_NONE, "i32.gt_u", 0) \
  X(i32_le_s, 0x4C, WASMHEX_NO_SUB, IMM_NONE, "i32.le_s", 0) \
  X(i32_le_u, 0x4D, WASMHEX_NO_SUB, IMM_NONE, "i32.le_u", 0) \
  X(i32_ge_s, 0x4E, WASMHEX_NO_SUB, IMM_NONE, "i32.ge_s", 0) \
  X(i32_ge_u, 0x4F, WASMHEX_NO_SUB, IMM_NONE, "i32.ge_u", 0) \
  X(i64_eqz, 0x50, WASMHEX_NO_SUB, IMM_NONE, "i64.eqz", 0) \
  X(i64_eq, 0x51, WASMHEX_NO_SUB, IMM_NONE, "i64.eq", 0) \
  X(i64_ne, 0x52, WASMHEX_NO_SUB, IMM_NONE, "i64.ne", 0) \
  X(i64_lt_s, 0x53, WASMHEX_NO_SUB, IMM_NONE, "i64.lt_s", 0) \
  X(i64_lt_u, 0x54, WASMHEX_NO_SUB, IMM_NONE, "i64.lt_u", 0) \
  X(i64_gt_s, 0x55, WASMHEX_NO_SUB, IMM_NONE, "i64.gt_s", 0) \
  X(i64_gt_u, 0x56, WASMHEX_NO_SUB, IMM_NONE, "i64.gt_u", 0) \
  X(i64_le_s, 0x57, WASMHEX_NO_SUB, IMM_NONE, "i64.le_s", 0) \
  X(i64_le_u, 0x58, WASMHEX_NO_SUB, IMM_NONE, "i64.le_u", 0) \
  X(i64_ge_s, 0x59, WASMHEX_NO_SUB, IMM_NONE, "i64.ge_s", 0) \
  X(i64_ge_u, 0x5A, WASMHEX_NO_SUB, IMM_NONE, "i64.ge_u", 0) \
  X(i64_const, 0x42, WASMHEX_NO_SUB, IMM_I64_CONST, "i64.const", 0) \
  X(f32_const, 0x43, WASMHEX_NO_SUB, IMM_F32_CONST, "f32.const", 0) \
  X(f64_const, 0x44, WASMHEX_NO_SUB, IMM_F64_CONST, "f64.const", 0) \
  X(i32_load, 0x28, WASMHEX_NO_SUB, IMM_MEMARG, "i32.load", CF_USE1) \
  X(i64_load, 0x29, WASMHEX_NO_SUB, IMM_MEMARG, "i64.load", CF_USE1) \
  X(f32_load, 0x2A, WASMHEX_NO_SUB, IMM_MEMARG, "f32.load", CF_USE1) \
  X(f64_load, 0x2B, WASMHEX_NO_SUB, IMM_MEMARG, "f64.load", CF_USE1) \
  X(i32_load8_s, 0x2C, WASMHEX_NO_SUB, IMM_MEMARG, "i32.load8_s", CF_USE1) \
  X(i32_load8_u, 0x2D, WASMHEX_NO_SUB, IMM_MEMARG, "i32.load8_u", CF_USE1) \
  X(i32_load16_s, 0x2E, WASMHEX_NO_SUB, IMM_MEMARG, "i32.load16_s", CF_USE1) \
  X(i32_load16_u, 0x2F, WASMHEX_NO_SUB, IMM_MEMARG, "i32.load16_u", CF_USE1) \
  X(i64_load8_s, 0x30, WASMHEX_NO_SUB, IMM_MEMARG, "i64.load8_s", CF_USE1) \
  X(i64_load8_u, 0x31, WASMHEX_NO_SUB, IMM_MEMARG, "i64.load8_u", CF_USE1) \
  X(i64_load16_s, 0x32, WASMHEX_NO_SUB, IMM_MEMARG, "i64.load16_s", CF_USE1) \
  X(i64_load16_u, 0x33, WASMHEX_NO_SUB, IMM_MEMARG, "i64.load16_u", CF_USE1) \
  X(i64_load32_s, 0x34, WASMHEX_NO_SUB, IMM_MEMARG, "i64.load32_s", CF_USE1) \
  X(i64_load32_u, 0x35, WASMHEX_NO_SUB, IMM_MEMARG, "i64.load32_u", CF_USE1) \
  X(i32_store, 0x36, WASMHEX_NO_SUB, IMM_MEMARG, "i32.store", CF_USE1) \
  X(i64_store, 0x37, WASMHEX_NO_SUB, IMM_MEMARG, "i64.store", CF_USE1) \
  X(f32_store, 0x38, WASMHEX_NO_SUB, IMM_MEMARG, "f32.store", CF_USE1) \
  X(f64_store, 0x39, WASMHEX_NO_SUB, IMM_MEMARG, "f64.store", CF_USE1) \
  X(i32_store8, 0x3A, WASMHEX_NO_SUB, IMM_MEMARG, "i32.store8", CF_USE1) \
  X(i32_store16, 0x3B, WASMHEX_NO_SUB, IMM_MEMARG, "i32.store16", CF_USE1) \
  X(i64_store8, 0x3C, WASMHEX_NO_SUB, IMM_MEMARG, "i64.store8", CF_USE1) \
  X(i64_store16, 0x3D, WASMHEX_NO_SUB, IMM_MEMARG, "i64.store16", CF_USE1) \
  X(i64_store32, 0x3E, WASMHEX_NO_SUB, IMM_MEMARG, "i64.store32", CF_USE1) \
  X(memory_size, 0x3F, WASMHEX_NO_SUB, IMM_MEM_SIZEGROW, "memory.size", 0) \
  X(memory_grow, 0x40, WASMHEX_NO_SUB, IMM_MEM_SIZEGROW, "memory.grow", 0) \
  X(memory_copy, 0xFC, 10, IMM_FC_SUB, "memory.copy", 0) \
  X(memory_fill, 0xFC, 11, IMM_FC_SUB, "memory.fill", 0) \
  X(memory_init, 0xFC, 8, IMM_FC_SUB, "memory.init", 0) \
  X(data_drop, 0xFC, 9, IMM_FC_SUB, "data.drop", 0) \
  X(call, 0x10, WASMHEX_NO_SUB, IMM_CALL, "call", CF_CALL) \
  X(call_unwind, 0x10, WASMHEX_NO_SUB, IMM_CALL_UNWIND, "call.unwind", CF_CALL|CF_STOP) \
  X(call_indirect, 0x11, WASMHEX_NO_SUB, IMM_CALL_INDIRECT, "call_indirect", CF_CALL) \
  X(return_call_indirect, 0x13, WASMHEX_NO_SUB, IMM_CALL_INDIRECT, "return_call_indirect", CF_CALL | CF_STOP) \
  X(return_call, 0x12, WASMHEX_NO_SUB, IMM_ULEB, "return_call", CF_CALL | CF_STOP) \
  X(call_ref, 0x14, WASMHEX_NO_SUB, IMM_ULEB, "call_ref", CF_CALL) \
  X(return_call_ref, 0x15, WASMHEX_NO_SUB, IMM_ULEB, "return_call_ref", CF_CALL | CF_STOP) \
  X(i32_add, 0x6A, WASMHEX_NO_SUB, IMM_NONE, "i32.add", 0) \
  X(i32_sub, 0x6B, WASMHEX_NO_SUB, IMM_NONE, "i32.sub", 0) \
  X(i32_mul, 0x6C, WASMHEX_NO_SUB, IMM_NONE, "i32.mul", 0) \
  X(i32_div_s, 0x6D, WASMHEX_NO_SUB, IMM_NONE, "i32.div_s", 0) \
  X(i32_div_u, 0x6E, WASMHEX_NO_SUB, IMM_NONE, "i32.div_u", 0) \
  X(i32_rem_s, 0x6F, WASMHEX_NO_SUB, IMM_NONE, "i32.rem_s", 0) \
  X(i32_rem_u, 0x70, WASMHEX_NO_SUB, IMM_NONE, "i32.rem_u", 0) \
  X(i32_shl, 0x74, WASMHEX_NO_SUB, IMM_NONE, "i32.shl", 0) \
  X(i32_shr_s, 0x75, WASMHEX_NO_SUB, IMM_NONE, "i32.shr_s", 0) \
  X(i32_shr_u, 0x76, WASMHEX_NO_SUB, IMM_NONE, "i32.shr_u", 0) \
  X(i32_rotl, 0x77, WASMHEX_NO_SUB, IMM_NONE, "i32.rotl", 0) \
  X(i32_rotr, 0x78, WASMHEX_NO_SUB, IMM_NONE, "i32.rotr", 0) \
  X(i32_and, 0x71, WASMHEX_NO_SUB, IMM_NONE, "i32.and", 0) \
  X(i32_or, 0x72, WASMHEX_NO_SUB, IMM_NONE, "i32.or", 0) \
  X(i32_xor, 0x73, WASMHEX_NO_SUB, IMM_NONE, "i32.xor", 0) \
  X(i64_add, 0x7C, WASMHEX_NO_SUB, IMM_NONE, "i64.add", 0) \
  X(i64_sub, 0x7D, WASMHEX_NO_SUB, IMM_NONE, "i64.sub", 0) \
  X(i64_mul, 0x7E, WASMHEX_NO_SUB, IMM_NONE, "i64.mul", 0) \
  X(i64_div_s, 0x7F, WASMHEX_NO_SUB, IMM_NONE, "i64.div_s", 0) \
  X(i64_div_u, 0x80, WASMHEX_NO_SUB, IMM_NONE, "i64.div_u", 0) \
  X(i64_rem_s, 0x81, WASMHEX_NO_SUB, IMM_NONE, "i64.rem_s", 0) \
  X(i64_rem_u, 0x82, WASMHEX_NO_SUB, IMM_NONE, "i64.rem_u", 0) \
  X(i64_and, 0x83, WASMHEX_NO_SUB, IMM_NONE, "i64.and", 0) \
  X(i64_or, 0x84, WASMHEX_NO_SUB, IMM_NONE, "i64.or", 0) \
  X(i64_xor, 0x85, WASMHEX_NO_SUB, IMM_NONE, "i64.xor", 0) \
  X(i64_shl, 0x86, WASMHEX_NO_SUB, IMM_NONE, "i64.shl", 0) \
  X(i64_shr_s, 0x87, WASMHEX_NO_SUB, IMM_NONE, "i64.shr_s", 0) \
  X(i64_shr_u, 0x88, WASMHEX_NO_SUB, IMM_NONE, "i64.shr_u", 0) \
  X(i64_rotl, 0x89, WASMHEX_NO_SUB, IMM_NONE, "i64.rotl", 0) \
  X(i64_rotr, 0x8A, WASMHEX_NO_SUB, IMM_NONE, "i64.rotr", 0) \
  X(f32_add, 0x92, WASMHEX_NO_SUB, IMM_NONE, "f32.add", 0) \
  X(f32_sub, 0x93, WASMHEX_NO_SUB, IMM_NONE, "f32.sub", 0) \
  X(f32_mul, 0x94, WASMHEX_NO_SUB, IMM_NONE, "f32.mul", 0) \
  X(f32_div, 0x95, WASMHEX_NO_SUB, IMM_NONE, "f32.div", 0) \
  X(f64_add, 0xA0, WASMHEX_NO_SUB, IMM_NONE, "f64.add", 0) \
  X(f64_sub, 0xA1, WASMHEX_NO_SUB, IMM_NONE, "f64.sub", 0) \
  X(f64_mul, 0xA2, WASMHEX_NO_SUB, IMM_NONE, "f64.mul", 0) \
  X(f64_div, 0xA3, WASMHEX_NO_SUB, IMM_NONE, "f64.div", 0) \
  X(f32_abs, 0x8B, WASMHEX_NO_SUB, IMM_NONE, "f32.abs", 0) \
  X(f32_neg, 0x8C, WASMHEX_NO_SUB, IMM_NONE, "f32.neg", 0) \
  X(f32_copysign, 0x98, WASMHEX_NO_SUB, IMM_NONE, "f32.copysign", 0) \
  X(f64_abs, 0x99, WASMHEX_NO_SUB, IMM_NONE, "f64.abs", 0) \
  X(f64_neg, 0x9A, WASMHEX_NO_SUB, IMM_NONE, "f64.neg", 0) \
  X(f64_copysign, 0xA6, WASMHEX_NO_SUB, IMM_NONE, "f64.copysign", 0) \
  X(f32_eq, 0x5B, WASMHEX_NO_SUB, IMM_NONE, "f32.eq", 0) \
  X(f32_ne, 0x5C, WASMHEX_NO_SUB, IMM_NONE, "f32.ne", 0) \
  X(f32_lt, 0x5D, WASMHEX_NO_SUB, IMM_NONE, "f32.lt", 0) \
  X(f32_gt, 0x5E, WASMHEX_NO_SUB, IMM_NONE, "f32.gt", 0) \
  X(f32_le, 0x5F, WASMHEX_NO_SUB, IMM_NONE, "f32.le", 0) \
  X(f32_ge, 0x60, WASMHEX_NO_SUB, IMM_NONE, "f32.ge", 0) \
  X(f64_eq, 0x61, WASMHEX_NO_SUB, IMM_NONE, "f64.eq", 0) \
  X(f64_ne, 0x62, WASMHEX_NO_SUB, IMM_NONE, "f64.ne", 0) \
  X(f64_lt, 0x63, WASMHEX_NO_SUB, IMM_NONE, "f64.lt", 0) \
  X(f64_gt, 0x64, WASMHEX_NO_SUB, IMM_NONE, "f64.gt", 0) \
  X(f64_le, 0x65, WASMHEX_NO_SUB, IMM_NONE, "f64.le", 0) \
  X(f64_ge, 0x66, WASMHEX_NO_SUB, IMM_NONE, "f64.ge", 0) \
  X(f32_ceil, 0x8D, WASMHEX_NO_SUB, IMM_NONE, "f32.ceil", 0) \
  X(f32_floor, 0x8E, WASMHEX_NO_SUB, IMM_NONE, "f32.floor", 0) \
  X(f32_trunc, 0x8F, WASMHEX_NO_SUB, IMM_NONE, "f32.trunc", 0) \
  X(f32_nearest, 0x90, WASMHEX_NO_SUB, IMM_NONE, "f32.nearest", 0) \
  X(f32_sqrt, 0x91, WASMHEX_NO_SUB, IMM_NONE, "f32.sqrt", 0) \
  X(f64_ceil, 0x9B, WASMHEX_NO_SUB, IMM_NONE, "f64.ceil", 0) \
  X(f64_floor, 0x9C, WASMHEX_NO_SUB, IMM_NONE, "f64.floor", 0) \
  X(f64_trunc, 0x9D, WASMHEX_NO_SUB, IMM_NONE, "f64.trunc", 0) \
  X(f64_nearest, 0x9E, WASMHEX_NO_SUB, IMM_NONE, "f64.nearest", 0) \
  X(f64_sqrt, 0x9F, WASMHEX_NO_SUB, IMM_NONE, "f64.sqrt", 0) \
  X(f32_min, 0x96, WASMHEX_NO_SUB, IMM_NONE, "f32.min", 0) \
  X(f32_max, 0x97, WASMHEX_NO_SUB, IMM_NONE, "f32.max", 0) \
  X(f64_min, 0xA4, WASMHEX_NO_SUB, IMM_NONE, "f64.min", 0) \
  X(f64_max, 0xA5, WASMHEX_NO_SUB, IMM_NONE, "f64.max", 0) \
  X(i32_trunc_sat_f32_s, 0xFC, 0, IMM_FC_SUB, "i32.trunc_sat_f32_s", 0) \
  X(i32_trunc_sat_f32_u, 0xFC, 1, IMM_FC_SUB, "i32.trunc_sat_f32_u", 0) \
  X(i32_trunc_sat_f64_s, 0xFC, 2, IMM_FC_SUB, "i32.trunc_sat_f64_s", 0) \
  X(i32_trunc_sat_f64_u, 0xFC, 3, IMM_FC_SUB, "i32.trunc_sat_f64_u", 0) \
  X(i64_trunc_sat_f32_s, 0xFC, 4, IMM_FC_SUB, "i64.trunc_sat_f32_s", 0) \
  X(i64_trunc_sat_f32_u, 0xFC, 5, IMM_FC_SUB, "i64.trunc_sat_f32_u", 0) \
  X(i64_trunc_sat_f64_s, 0xFC, 6, IMM_FC_SUB, "i64.trunc_sat_f64_s", 0) \
  X(i64_trunc_sat_f64_u, 0xFC, 7, IMM_FC_SUB, "i64.trunc_sat_f64_u", 0) \
  X(i32_trunc_f32_s, 0xA8, WASMHEX_NO_SUB, IMM_NONE, "i32.trunc_f32_s", 0) \
  X(i32_trunc_f32_u, 0xA9, WASMHEX_NO_SUB, IMM_NONE, "i32.trunc_f32_u", 0) \
  X(i32_trunc_f64_s, 0xAA, WASMHEX_NO_SUB, IMM_NONE, "i32.trunc_f64_s", 0) \
  X(i32_trunc_f64_u, 0xAB, WASMHEX_NO_SUB, IMM_NONE, "i32.trunc_f64_u", 0) \
  X(i64_trunc_f32_s, 0xAE, WASMHEX_NO_SUB, IMM_NONE, "i64.trunc_f32_s", 0) \
  X(i64_trunc_f32_u, 0xAF, WASMHEX_NO_SUB, IMM_NONE, "i64.trunc_f32_u", 0) \
  X(i64_trunc_f64_s, 0xB0, WASMHEX_NO_SUB, IMM_NONE, "i64.trunc_f64_s", 0) \
  X(i64_trunc_f64_u, 0xB1, WASMHEX_NO_SUB, IMM_NONE, "i64.trunc_f64_u", 0) \
  X(i32_reinterpret_f32, 0xBC, WASMHEX_NO_SUB, IMM_NONE, "i32.reinterpret_f32", 0) \
  X(i64_reinterpret_f64, 0xBD, WASMHEX_NO_SUB, IMM_NONE, "i64.reinterpret_f64", 0) \
  X(f32_reinterpret_i32, 0xBE, WASMHEX_NO_SUB, IMM_NONE, "f32.reinterpret_i32", 0) \
  X(f64_reinterpret_i64, 0xBF, WASMHEX_NO_SUB, IMM_NONE, "f64.reinterpret_i64", 0) \
  X(f32_convert_i32_s, 0xB2, WASMHEX_NO_SUB, IMM_NONE, "f32.convert_i32_s", 0) \
  X(f32_convert_i32_u, 0xB3, WASMHEX_NO_SUB, IMM_NONE, "f32.convert_i32_u", 0) \
  X(f32_convert_i64_s, 0xB4, WASMHEX_NO_SUB, IMM_NONE, "f32.convert_i64_s", 0) \
  X(f32_convert_i64_u, 0xB5, WASMHEX_NO_SUB, IMM_NONE, "f32.convert_i64_u", 0) \
  X(f32_demote_f64, 0xB6, WASMHEX_NO_SUB, IMM_NONE, "f32.demote_f64", 0) \
  X(f64_convert_i32_s, 0xB7, WASMHEX_NO_SUB, IMM_NONE, "f64.convert_i32_s", 0) \
  X(f64_convert_i32_u, 0xB8, WASMHEX_NO_SUB, IMM_NONE, "f64.convert_i32_u", 0) \
  X(f64_convert_i64_s, 0xB9, WASMHEX_NO_SUB, IMM_NONE, "f64.convert_i64_s", 0) \
  X(f64_convert_i64_u, 0xBA, WASMHEX_NO_SUB, IMM_NONE, "f64.convert_i64_u", 0) \
  X(f64_promote_f32, 0xBB, WASMHEX_NO_SUB, IMM_NONE, "f64.promote_f32", 0) \
  X(i32_wrap_i64, 0xA7, WASMHEX_NO_SUB, IMM_NONE, "i32.wrap_i64", 0) \
  X(i64_extend_i32_s, 0xAC, WASMHEX_NO_SUB, IMM_NONE, "i64.extend_i32_s", 0) \
  X(i64_extend_i32_u, 0xAD, WASMHEX_NO_SUB, IMM_NONE, "i64.extend_i32_u", 0) \
  X(i32_extend8_s, 0xC0, WASMHEX_NO_SUB, IMM_NONE, "i32.extend8_s", 0) \
  X(i32_extend16_s, 0xC1, WASMHEX_NO_SUB, IMM_NONE, "i32.extend16_s", 0) \
  X(i64_extend8_s, 0xC2, WASMHEX_NO_SUB, IMM_NONE, "i64.extend8_s", 0) \
  X(i64_extend16_s, 0xC3, WASMHEX_NO_SUB, IMM_NONE, "i64.extend16_s", 0) \
  X(i64_extend32_s, 0xC4, WASMHEX_NO_SUB, IMM_NONE, "i64.extend32_s", 0) \
  X(ref_null, 0xD0, WASMHEX_NO_SUB, IMM_REF_NULL, "ref.null", CF_USE1) \
  X(ref_is_null, 0xD1, WASMHEX_NO_SUB, IMM_NONE, "ref.is_null", 0) \
  X(ref_func, 0xD2, WASMHEX_NO_SUB, IMM_SPECIAL, "ref.func", CF_USE1) \
  X(ref_as_non_null, 0xD4, WASMHEX_NO_SUB, IMM_NONE, "ref.as_non_null", 0) \
  X(v128_load, 0xFD, 0, IMM_FD_SUB, "v128.load", CF_USE1) \
  X(v128_store, 0xFD, 11, IMM_FD_SUB, "v128.store", CF_USE1) \
  X(v128_load32_zero, 0xFD, 92, IMM_FD_SUB, "v128.load32_zero", CF_USE1) \
  X(v128_load64_zero, 0xFD, 93, IMM_FD_SUB, "v128.load64_zero", CF_USE1) \
  X(v128_load8_splat, 0xFD, 7, IMM_FD_SUB, "v128.load8_splat", CF_USE1) \
  X(v128_load16_splat, 0xFD, 8, IMM_FD_SUB, "v128.load16_splat", CF_USE1) \
  X(v128_load32_splat, 0xFD, 9, IMM_FD_SUB, "v128.load32_splat", CF_USE1) \
  X(v128_load64_splat, 0xFD, 10, IMM_FD_SUB, "v128.load64_splat", CF_USE1) \
  X(atomic, 0xFE, WASMHEX_NO_SUB, IMM_FE_SUB, "atomic", 0) \
  X(ref_eq, 0xD3, WASMHEX_NO_SUB, IMM_NONE, "ref.eq", 0) \
  X(ref_i31, 0xFB, 28, IMM_FB_SUB, "ref.i31", 0) \
  X(i31_get_s, 0xFB, 29, IMM_FB_SUB, "i31.get_s", 0) \
  X(i31_get_u, 0xFB, 30, IMM_FB_SUB, "i31.get_u", 0) \
  X(v128_load8_lane, 0xFD, 84, IMM_FD_SUB, "v128.load8_lane", CF_USE1|CF_USE2) \
  X(v128_load16_lane, 0xFD, 85, IMM_FD_SUB, "v128.load16_lane", CF_USE1|CF_USE2) \
  X(v128_load32_lane, 0xFD, 86, IMM_FD_SUB, "v128.load32_lane", CF_USE1|CF_USE2) \
  X(v128_load64_lane, 0xFD, 87, IMM_FD_SUB, "v128.load64_lane", CF_USE1|CF_USE2) \
  X(v128_store8_lane, 0xFD, 88, IMM_FD_SUB, "v128.store8_lane", CF_USE1|CF_USE2) \
  X(v128_store16_lane, 0xFD, 89, IMM_FD_SUB, "v128.store16_lane", CF_USE1|CF_USE2) \
  X(v128_store32_lane, 0xFD, 90, IMM_FD_SUB, "v128.store32_lane", CF_USE1|CF_USE2) \
  X(v128_store64_lane, 0xFD, 91, IMM_FD_SUB, "v128.store64_lane", CF_USE1|CF_USE2) \
  X(v128_load8x8_s, 0xFD, 1, IMM_FD_SUB, "v128.load8x8_s", CF_USE1) \
  X(v128_load8x8_u, 0xFD, 2, IMM_FD_SUB, "v128.load8x8_u", CF_USE1) \
  X(v128_load16x4_s, 0xFD, 3, IMM_FD_SUB, "v128.load16x4_s", CF_USE1) \
  X(v128_load16x4_u, 0xFD, 4, IMM_FD_SUB, "v128.load16x4_u", CF_USE1) \
  X(v128_load32x2_s, 0xFD, 5, IMM_FD_SUB, "v128.load32x2_s", CF_USE1) \
  X(v128_load32x2_u, 0xFD, 6, IMM_FD_SUB, "v128.load32x2_u", CF_USE1) \
  X(table_size, 0xFC, 16, IMM_FC_SUB, "table.size", CF_USE1) \
  X(call_dispatch_bounds, 0xFA, WASMHEX_NO_SUB, IMM_NONE, "call.check_bounds", CF_JUMP) \
  X(branch_goto, 0xFF, WASMHEX_NO_SUB, IMM_ULEB, "br.goto", CF_USE1|CF_JUMP|CF_STOP)

} // namespace wasmhex::opcodes

// clang-format on
