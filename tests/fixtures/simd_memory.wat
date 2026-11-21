(module
  (memory (export "memory") 1)
  (func (export "load") (param i32) (result i32)
    local.get 0
    v128.load offset=4
    i32x4.extract_lane 3)
  (func (export "load8x8_s") (param i32) (result i32)
    local.get 0
    v128.load8x8_s
    i16x8.extract_lane_s 7)
  (func (export "load8x8_u") (param i32) (result i32)
    local.get 0
    v128.load8x8_u
    i16x8.extract_lane_u 7)
  (func (export "load16x4_s") (param i32) (result i32)
    local.get 0
    v128.load16x4_s
    i32x4.extract_lane 3)
  (func (export "load16x4_u") (param i32) (result i32)
    local.get 0
    v128.load16x4_u
    i32x4.extract_lane 3)
  (func (export "load32x2_s") (param i32) (result i64)
    local.get 0
    v128.load32x2_s
    i64x2.extract_lane 1)
  (func (export "load32x2_u") (param i32) (result i64)
    local.get 0
    v128.load32x2_u
    i64x2.extract_lane 1)
  (func (export "load8_splat") (param i32) (result i32)
    local.get 0
    v128.load8_splat
    i8x16.extract_lane_u 15)
  (func (export "load16_splat") (param i32) (result i32)
    local.get 0
    v128.load16_splat
    i16x8.extract_lane_u 7)
  (func (export "load32_splat") (param i32) (result i32)
    local.get 0
    v128.load32_splat
    i32x4.extract_lane 3)
  (func (export "load64_splat") (param i32) (result i64)
    local.get 0
    v128.load64_splat
    i64x2.extract_lane 1)
  (func (export "load32_zero") (param i32) (result i32)
    local.get 0
    v128.load32_zero
    i32x4.extract_lane 3)
  (func (export "load64_zero") (param i32) (result i64)
    local.get 0
    v128.load64_zero
    i64x2.extract_lane 1)
  (func (export "store") (param i32 i32)
    local.get 0
    local.get 1 i32x4.splat
    v128.store)
  (func (export "load8_lane") (param i32 i32) (result i32)
    local.get 0
    local.get 1 i32x4.splat
    v128.load8_lane 3
    i8x16.extract_lane_u 3)
  (func (export "load16_lane") (param i32 i32) (result i32)
    local.get 0
    local.get 1 i32x4.splat
    v128.load16_lane 7
    i16x8.extract_lane_u 7)
  (func (export "load32_lane") (param i32 i32) (result i32)
    local.get 0
    local.get 1 i32x4.splat
    v128.load32_lane 3
    i32x4.extract_lane 3)
  (func (export "load64_lane") (param i32 i64) (result i64)
    local.get 0
    local.get 1 i64x2.splat
    v128.load64_lane 1
    i64x2.extract_lane 1)
  (func (export "store16_lane") (param i32 i32)
    local.get 0
    local.get 1 i16x8.splat
    v128.store16_lane 3)
  (func (export "store8_lane") (param i32 i32)
    local.get 0
    local.get 1 i32x4.splat
    v128.store8_lane 15)
  (func (export "store32_lane") (param i32 i32)
    local.get 0
    local.get 1 i32x4.splat
    v128.store32_lane 2)
  (func (export "store64_lane") (param i32 i64)
    local.get 0
    local.get 1 i64x2.splat
    v128.store64_lane 1))
