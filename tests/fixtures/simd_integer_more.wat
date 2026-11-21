(module
  (func (export "i16_abs") (param i32) (result i32)
    local.get 0 i16x8.splat
    i16x8.abs
    i16x8.extract_lane_u 7)
  (func (export "i16_neg") (param i32) (result i32)
    local.get 0 i16x8.splat
    i16x8.neg
    i16x8.extract_lane_u 7)
  (func (export "i64_abs") (param i64) (result i64)
    local.get 0 i64x2.splat
    i64x2.abs
    i64x2.extract_lane 1)
  (func (export "i64_neg") (param i64) (result i64)
    local.get 0 i64x2.splat
    i64x2.neg
    i64x2.extract_lane 1)
  (func (export "i64_all_true") (param i64) (result i32)
    local.get 0 i64x2.splat
    i64x2.all_true)
  (func (export "i64_bitmask") (param i64) (result i32)
    local.get 0 i64x2.splat
    i64x2.bitmask)
  (func (export "andnot") (param i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    v128.andnot
    i32x4.extract_lane 3)
  (func (export "bitselect") (param i32 i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    local.get 2 i32x4.splat
    v128.bitselect
    i32x4.extract_lane 3)
  (func (export "extmul_i8_low_s") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i16x8.extmul_low_i8x16_s
    i16x8.extract_lane_s 7)
  (func (export "extmul_i8_high_s") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i16x8.extmul_high_i8x16_s
    i16x8.extract_lane_s 7)
  (func (export "extmul_i8_low_u") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i16x8.extmul_low_i8x16_u
    i16x8.extract_lane_u 7)
  (func (export "extmul_i8_high_u") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i16x8.extmul_high_i8x16_u
    i16x8.extract_lane_u 7)
  (func (export "extmul_i16_low_s") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i32x4.extmul_low_i16x8_s
    i32x4.extract_lane 3)
  (func (export "extmul_i16_high_s") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i32x4.extmul_high_i16x8_s
    i32x4.extract_lane 3)
  (func (export "extmul_i16_low_u") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i32x4.extmul_low_i16x8_u
    i32x4.extract_lane 3)
  (func (export "extmul_i16_high_u") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i32x4.extmul_high_i16x8_u
    i32x4.extract_lane 3)
  (func (export "extmul_i32_low_s") (param i32 i32) (result i64)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    i64x2.extmul_low_i32x4_s
    i64x2.extract_lane 1)
  (func (export "extmul_i32_high_s") (param i32 i32) (result i64)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    i64x2.extmul_high_i32x4_s
    i64x2.extract_lane 1)
  (func (export "extmul_i32_low_u") (param i32 i32) (result i64)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    i64x2.extmul_low_i32x4_u
    i64x2.extract_lane 1)
  (func (export "extmul_i32_high_u") (param i32 i32) (result i64)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    i64x2.extmul_high_i32x4_u
    i64x2.extract_lane 1)
  (func (export "dot_i16") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i32x4.dot_i16x8_s
    i32x4.extract_lane 3)
  (func (export "extmul_i8_pattern") (result i32)
    v128.const i32x4 0x04030201 0x08070605 0x0c0b0a09 0x100f0e0d
    i32.const 1 i8x16.splat
    i16x8.extmul_low_i8x16_u
    i16x8.extract_lane_u 1)
  (func (export "extmul_i16_pattern") (result i32)
    v128.const i32x4 0x00020001 0x00040003 0x00060005 0x00080007
    i32.const 1 i16x8.splat
    i32x4.extmul_low_i16x8_u
    i32x4.extract_lane 1)
  (func (export "extmul_i32_pattern") (result i64)
    v128.const i32x4 1 2 3 4
    i32.const 1 i32x4.splat
    i64x2.extmul_low_i32x4_u
    i64x2.extract_lane 1))
