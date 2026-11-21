// WebAssembly SIMD (0xFD) validation
// tables — header-only extension for function_validator.cpp.
// The tables follow the FINAL SIMD opcode numbering (WebAssembly/simd
// NewOpcodes.md — cross-checked against wabt's encoder on this machine:
// fd 00 v128.load … fd 5b v128.store64_lane, fd f8.. conversions; the
// earlier draft numbering in the first version of this header never
// matched shipped encoders). Relaxed SIMD (sub-opcode >= 0x100) stays
// unsupported on purpose: no fixture or corpus module exercises it with
// code and the family is not part of the frozen base spec.
// Each entry carries: the operand signature (value-type bytes, params in
// source order — the caller pops top-of-stack LAST), the immediate shape
// (memarg / memarg+lane / lane / 16 shuffle bytes / 16 const bytes), the
// access width in bytes (natural memarg alignment = log2(width)) and the
// lane bound for lane immediates.
#pragma once

#include "../../types.hpp"

#include <cstdint>

namespace wasmhex::loader
{

// Canonical value-type bytes (spec):
constexpr uchar simd_vt_i32 = 0x7F;
constexpr uchar simd_vt_i64 = 0x7E;
constexpr uchar simd_vt_f32 = 0x7D;
constexpr uchar simd_vt_f64 = 0x7C;
constexpr uchar simd_vt_v128 = 0x7B;

enum simd_imm_kind_t : uint8_t
{
  SIMD_IMM_NONE = 0,
  SIMD_IMM_MEMARG,      // align (LEB) + offset (LEB)
  SIMD_IMM_MEMARG_LANE, // memarg + lane byte (load/store lane forms)
  SIMD_IMM_LANE,        // single lane byte (extract/replace)
  SIMD_IMM_SHUFFLE,     // 16 lane index bytes
  SIMD_IMM_CONST16      // 16 constant bytes (v128.const)
};

struct simd_op_info_t
{
  bool known = false;
  simd_imm_kind_t immediate = SIMD_IMM_NONE;
  // Operand signature in source order: params[0] is the deepest operand.
  // At most 3 operands in the SIMD opcode set (bitselect, laneselect).
  uint8_t param_count = 0;
  uint8_t result_count = 0;
  uchar params[3] = {};
  uchar results[1] = {};
  uint16_t width = 0; // access width in bytes (0: no memory operand)
  uint8_t lanes = 0;  // lane bound for LANE / MEMARG_LANE immediates
};

namespace simd_detail
{
constexpr simd_op_info_t op(std::initializer_list<uchar> params,
                            std::initializer_list<uchar> results,
                            simd_imm_kind_t imm = SIMD_IMM_NONE, uint16_t width = 0,
                            uint8_t lanes = 0)
{
  simd_op_info_t info;
  info.known = true;
  info.immediate = imm;
  info.width = width;
  info.lanes = lanes;
  uint8_t i = 0;
  for ( uchar p : params )
    if ( i < 3 )
      info.params[i++] = p;
  info.param_count = i;
  i = 0;
  for ( uchar r : results )
    if ( i < 1 )
      info.results[i++] = r;
  info.result_count = i;
  return info;
}
} // namespace simd_detail

// Signature of a SIMD sub-opcode (the LEB value after the 0xFD prefix).
// Unknown sub-opcodes return known=false; the caller reports a malformed
// instruction. Sub-opcodes >= 0x100 (relaxed SIMD) are left unknown.
inline simd_op_info_t simd_lookup(uint32 sub)
{
  using namespace simd_detail;
  constexpr uchar I = simd_vt_i32, L = simd_vt_i64;
  constexpr uchar F = simd_vt_f32, D = simd_vt_f64;
  constexpr uchar V = simd_vt_v128;
  switch ( sub )
  {
    // Loads 0x00..0x0A: [i32] -> v128, memarg.
    case 0x00: return op({ I }, { V }, SIMD_IMM_MEMARG, 16);
    case 0x01: // load8x8_s
    case 0x02: // load8x8_u
    case 0x03: // load16x4_s
    case 0x04: // load16x4_u
    case 0x05: // load32x2_s
    case 0x06: // load32x2_u
      return op({ I }, { V }, SIMD_IMM_MEMARG, 8);
    case 0x07: return op({ I }, { V }, SIMD_IMM_MEMARG, 1); // load8_splat
    case 0x08: return op({ I }, { V }, SIMD_IMM_MEMARG, 2); // load16_splat
    case 0x09: return op({ I }, { V }, SIMD_IMM_MEMARG, 4); // load32_splat
    case 0x0A:
      return op({ I }, { V }, SIMD_IMM_MEMARG, 8); // load64_splat
    // v128.store 0x0B: [i32, v128], memarg.
    case 0x0B: return op({ I, V }, {}, SIMD_IMM_MEMARG, 16);
    // v128.const / shuffle / swizzle.
    case 0x0C: return op({}, { V }, SIMD_IMM_CONST16);
    case 0x0D: return op({ V, V }, { V }, SIMD_IMM_SHUFFLE);
    case 0x0E: return op({ V, V }, { V });
    // Splats 0x0F..0x14.
    case 0x0F:                          // i8x16.splat
    case 0x10:                          // i16x8.splat
    case 0x11: return op({ I }, { V }); // i32x4.splat
    case 0x12: return op({ L }, { V }); // i64x2.splat
    case 0x13: return op({ F }, { V }); // f32x4.splat
    case 0x14:
      return op({ D }, { V }); // f64x2.splat
    // Extract lanes 0x15..0x22: [v128] -> scalar, lane immediate.
    case 0x15:                                                // i8x16.extract_lane_s
    case 0x16: return op({ V }, { I }, SIMD_IMM_LANE, 0, 16); // _u
    case 0x18:                                                // i16x8.extract_lane_s
    case 0x19: return op({ V }, { I }, SIMD_IMM_LANE, 0, 8);  // _u
    case 0x1B: return op({ V }, { I }, SIMD_IMM_LANE, 0, 4);  // i32x4
    case 0x1D: return op({ V }, { L }, SIMD_IMM_LANE, 0, 2);  // i64x2
    case 0x1F: return op({ V }, { F }, SIMD_IMM_LANE, 0, 4);  // f32x4
    case 0x21:
      return op({ V }, { D }, SIMD_IMM_LANE, 0, 2); // f64x2
    // Replace lanes 0x17..0x22: [v128, scalar] -> v128, lane immediate.
    case 0x17: return op({ V, I }, { V }, SIMD_IMM_LANE, 0, 16); // i8x16
    case 0x1A: return op({ V, I }, { V }, SIMD_IMM_LANE, 0, 8);  // i16x8
    case 0x1C: return op({ V, I }, { V }, SIMD_IMM_LANE, 0, 4);  // i32x4
    case 0x1E: return op({ V, L }, { V }, SIMD_IMM_LANE, 0, 2);  // i64x2
    case 0x20: return op({ V, F }, { V }, SIMD_IMM_LANE, 0, 4);  // f32x4
    case 0x22:
      return op({ V, D }, { V }, SIMD_IMM_LANE, 0, 2); // f64x2
    // Integer/float comparisons 0x23..0x4C: [v128, v128] -> v128.
    case 0x23:
    case 0x24:
    case 0x25:
    case 0x26:
    case 0x27:
    case 0x28:
    case 0x29:
    case 0x2A:
    case 0x2B:
    case 0x2C:
    case 0x2D:
    case 0x2E:
    case 0x2F:
    case 0x30:
    case 0x31:
    case 0x32:
    case 0x33:
    case 0x34:
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
    case 0x39:
    case 0x3A:
    case 0x3B:
    case 0x3C:
    case 0x3D:
    case 0x3E:
    case 0x3F:
    case 0x40:
    case 0x41:
    case 0x42:
    case 0x43:
    case 0x44:
    case 0x45:
    case 0x46:
    case 0x47:
    case 0x48:
    case 0x49:
    case 0x4A:
    case 0x4B:
    case 0x4C: return op({ V, V }, { V });
    // v128 bitwise 0x4D..0x53.
    case 0x4D: return op({ V }, { V });       // v128.not
    case 0x4E:                                // and
    case 0x4F:                                // andnot
    case 0x50:                                // or
    case 0x51: return op({ V, V }, { V });    // xor
    case 0x52: return op({ V, V, V }, { V }); // v128.bitselect
    case 0x53:
      return op({ V }, { I }); // v128.any_true
    // Lane load/store 0x54..0x5B: [i32, v128], memarg + lane immediate.
    case 0x54: return op({ I, V }, { V }, SIMD_IMM_MEMARG_LANE, 1, 16);
    case 0x55: return op({ I, V }, { V }, SIMD_IMM_MEMARG_LANE, 2, 8);
    case 0x56: return op({ I, V }, { V }, SIMD_IMM_MEMARG_LANE, 4, 4);
    case 0x57: return op({ I, V }, { V }, SIMD_IMM_MEMARG_LANE, 8, 2);
    case 0x58: return op({ I, V }, {}, SIMD_IMM_MEMARG_LANE, 1, 16);
    case 0x59: return op({ I, V }, {}, SIMD_IMM_MEMARG_LANE, 2, 8);
    case 0x5A: return op({ I, V }, {}, SIMD_IMM_MEMARG_LANE, 4, 4);
    case 0x5B: return op({ I, V }, {}, SIMD_IMM_MEMARG_LANE, 8, 2);
    // Zero loads 0x5C/0x5D: [i32] -> v128, memarg.
    case 0x5C: return op({ I }, { V }, SIMD_IMM_MEMARG, 4);
    case 0x5D: return op({ I }, { V }, SIMD_IMM_MEMARG, 8);
    // Float conversions 0x5E/0x5F: [v128] -> v128.
    case 0x5E:
    case 0x5F: return op({ V }, { V });
    default: break;
  }
  // 0x60..0xFF: the four shape families (i8x16 0x60.., i16x8 0x80..,
  // i32x4 0xA0.., i64x2 0xC0.. and the float blocks 0xE0..0xFF).
  if ( sub >= 0x60 && sub <= 0xFF )
  {
    // Shifts (shl/shr_s/shr_u): [v128, i32] -> v128.
    if ( sub == 0x6B || sub == 0x6C || sub == 0x6D || sub == 0x8B || sub == 0x8C || sub == 0x8D
         || sub == 0xAB || sub == 0xAC || sub == 0xAD || sub == 0xCB || sub == 0xCC || sub == 0xCD )
      return op({ V, I }, { V });
    // all_true / bitmask: [v128] -> i32 (one pair per shape family).
    if ( sub == 0x63 || sub == 0x64 || sub == 0x83 || sub == 0x84 || sub == 0xA3 || sub == 0xA4
         || sub == 0xC3 || sub == 0xC4 )
      return op({ V }, { I });
    // Unary [v128] -> v128: abs/neg/popcnt, f32x4/f64x2 rounding
    // (ceil/floor/trunc/nearest), extend_*, extadd_pairwise_*,
    // f32x4/f64x2 abs/neg/sqrt and the conversions 0xF8..0xFF.
    if ( sub == 0x60 || sub == 0x61 || sub == 0x62 || sub == 0x67 || sub == 0x68 || sub == 0x69
         || sub == 0x6A || sub == 0x74 || sub == 0x75 || sub == 0x7A || sub == 0x7C || sub == 0x7D
         || sub == 0x7E || sub == 0x7F || sub == 0x80 || sub == 0x81 || sub == 0x87 || sub == 0x88
         || sub == 0x89 || sub == 0x8A || sub == 0x94 || sub == 0xA0 || sub == 0xA1 || sub == 0xA7
         || sub == 0xA8 || sub == 0xA9 || sub == 0xAA || sub == 0xC0 || sub == 0xC1 || sub == 0xC7
         || sub == 0xC8 || sub == 0xC9 || sub == 0xCA || sub == 0xE0 || sub == 0xE1 || sub == 0xE3
         || sub == 0xEC || sub == 0xED || sub == 0xEF || sub >= 0xF8 )
      return op({ V }, { V });
    // Binary [v128, v128] -> v128: narrow, q15mulr_sat, plain and
    // saturated add/sub, mul, min/max, avgr_u, dot, extmul_*, the i64x2
    // integer comparisons and float arithmetic/pmin/pmax.
    if ( sub == 0x65 || sub == 0x66 || sub == 0x6E || sub == 0x6F || sub == 0x70 || sub == 0x71
         || sub == 0x72 || sub == 0x73 || sub == 0x76 || sub == 0x77 || sub == 0x78 || sub == 0x79
         || sub == 0x7B || sub == 0x82 || sub == 0x85 || sub == 0x86 || sub == 0x8E || sub == 0x8F
         || sub == 0x90 || sub == 0x91 || sub == 0x92 || sub == 0x93 || sub == 0x95 || sub == 0x96
         || sub == 0x97 || sub == 0x98 || sub == 0x99 || sub == 0x9B || sub == 0x9C || sub == 0x9D
         || sub == 0x9E || sub == 0x9F || sub == 0xAE || sub == 0xB1 || sub == 0xB5 || sub == 0xB6
         || sub == 0xB7 || sub == 0xB8 || sub == 0xB9 || sub == 0xBA || sub == 0xBC || sub == 0xBD
         || sub == 0xBE || sub == 0xBF || sub == 0xCE || sub == 0xD1 || sub == 0xD5 || sub == 0xD6
         || sub == 0xD7 || sub == 0xD8 || sub == 0xD9 || sub == 0xDA || sub == 0xDB || sub == 0xDC
         || sub == 0xDD || sub == 0xDE || sub == 0xDF || sub == 0xE4 || sub == 0xE5 || sub == 0xE6
         || sub == 0xE7 || sub == 0xE8 || sub == 0xE9 || sub == 0xEA || sub == 0xEB || sub == 0xF0
         || sub == 0xF1 || sub == 0xF2 || sub == 0xF3 || sub == 0xF4 || sub == 0xF5 || sub == 0xF6
         || sub == 0xF7 )
      return op({ V, V }, { V });
    // The empty spec slots (0x9A, 0xA2, 0xA5/0xA6, 0xAF/0xB0,
    // 0xB2..0xB4, 0xBB, 0xC2, 0xC5/0xC6, 0xCF/0xD0, 0xD2..0xD4, 0xE2,
    // 0xEE) fall through: an encoder must never emit them.
  }
  return simd_op_info_t{}; // unknown (incl. relaxed SIMD >= 0x100)
}

// Shuffle indices must address the concatenated 32-byte operand pair.
inline bool simd_shuffle_indices_ok(const uchar *indices)
{
  for ( size_t i = 0; i < 16; ++i )
    if ( indices[i] >= 32 )
      return false;
  return true;
}

} // namespace wasmhex::loader
