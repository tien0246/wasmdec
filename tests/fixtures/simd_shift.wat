(module
  (func (export "i8_shl") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1
    i8x16.shl
    i8x16.extract_lane_u 15)
  (func (export "i8_shr_s") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1
    i8x16.shr_s
    i8x16.extract_lane_s 15)
  (func (export "i8_shr_u") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1
    i8x16.shr_u
    i8x16.extract_lane_u 15)
  (func (export "i16_shl") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1
    i16x8.shl
    i16x8.extract_lane_u 7)
  (func (export "i16_shr_s") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1
    i16x8.shr_s
    i16x8.extract_lane_s 7)
  (func (export "i16_shr_u") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1
    i16x8.shr_u
    i16x8.extract_lane_u 7)
  (func (export "i32_shl") (param i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1
    i32x4.shl
    i32x4.extract_lane 3)
  (func (export "i32_shr_s") (param i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1
    i32x4.shr_s
    i32x4.extract_lane 3)
  (func (export "i32_shr_u") (param i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1
    i32x4.shr_u
    i32x4.extract_lane 3)
  (func (export "i64_shl") (param i64 i32) (result i64)
    local.get 0 i64x2.splat
    local.get 1
    i64x2.shl
    i64x2.extract_lane 1)
  (func (export "i64_shr_s") (param i64 i32) (result i64)
    local.get 0 i64x2.splat
    local.get 1
    i64x2.shr_s
    i64x2.extract_lane 1)
  (func (export "i64_shr_u") (param i64 i32) (result i64)
    local.get 0 i64x2.splat
    local.get 1
    i64x2.shr_u
    i64x2.extract_lane 1))
