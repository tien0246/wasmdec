(module
  (func (export "extadd_i8_s") (param i32) (result i32)
    local.get 0 i8x16.splat
    i16x8.extadd_pairwise_i8x16_s
    i16x8.extract_lane_s 7)
  (func (export "extadd_i8_u") (param i32) (result i32)
    local.get 0 i8x16.splat
    i16x8.extadd_pairwise_i8x16_u
    i16x8.extract_lane_u 7)
  (func (export "extadd_i16_s") (param i32) (result i32)
    local.get 0 i16x8.splat
    i32x4.extadd_pairwise_i16x8_s
    i32x4.extract_lane 3)
  (func (export "extadd_i16_u") (param i32) (result i32)
    local.get 0 i16x8.splat
    i32x4.extadd_pairwise_i16x8_u
    i32x4.extract_lane 3)
  (func (export "extend_i8_low_s") (param i32) (result i32)
    local.get 0 i8x16.splat
    i16x8.extend_low_i8x16_s
    i16x8.extract_lane_s 7)
  (func (export "extend_i8_high_s") (param i32) (result i32)
    local.get 0 i8x16.splat
    i16x8.extend_high_i8x16_s
    i16x8.extract_lane_s 7)
  (func (export "extend_i8_low_u") (param i32) (result i32)
    local.get 0 i8x16.splat
    i16x8.extend_low_i8x16_u
    i16x8.extract_lane_u 7)
  (func (export "extend_i8_high_u") (param i32) (result i32)
    local.get 0 i8x16.splat
    i16x8.extend_high_i8x16_u
    i16x8.extract_lane_u 7)
  (func (export "extend_i16_low_s") (param i32) (result i32)
    local.get 0 i16x8.splat
    i32x4.extend_low_i16x8_s
    i32x4.extract_lane 3)
  (func (export "extend_i16_high_s") (param i32) (result i32)
    local.get 0 i16x8.splat
    i32x4.extend_high_i16x8_s
    i32x4.extract_lane 3)
  (func (export "extend_i16_low_u") (param i32) (result i32)
    local.get 0 i16x8.splat
    i32x4.extend_low_i16x8_u
    i32x4.extract_lane 3)
  (func (export "extend_i16_high_u") (param i32) (result i32)
    local.get 0 i16x8.splat
    i32x4.extend_high_i16x8_u
    i32x4.extract_lane 3)
  (func (export "extend_i32_low_s") (param i32) (result i64)
    local.get 0 i32x4.splat
    i64x2.extend_low_i32x4_s
    i64x2.extract_lane 1)
  (func (export "extend_i32_high_s") (param i32) (result i64)
    local.get 0 i32x4.splat
    i64x2.extend_high_i32x4_s
    i64x2.extract_lane 1)
  (func (export "extend_i32_low_u") (param i32) (result i64)
    local.get 0 i32x4.splat
    i64x2.extend_low_i32x4_u
    i64x2.extract_lane 1)
  (func (export "extend_i32_high_u") (param i32) (result i64)
    local.get 0 i32x4.splat
    i64x2.extend_high_i32x4_u
    i64x2.extract_lane 1)
  (func (export "extend_i8_pattern") (result i32)
    v128.const i32x4 0x0201807f 0x06050403 0x0a090807 0x0e0d0c0b
    i16x8.extend_low_i8x16_u
    i16x8.extract_lane_u 1)
  (func (export "extend_i16_pattern") (result i32)
    v128.const i32x4 0x00020001 0x00040003 0x00060005 0x00080007
    i32x4.extend_low_i16x8_u
    i32x4.extract_lane 1)
  (func (export "extend_i32_pattern") (result i64)
    v128.const i32x4 1 2 3 4
    i64x2.extend_low_i32x4_u
    i64x2.extract_lane 1))
