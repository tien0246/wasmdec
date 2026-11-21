(module
  (func (export "f32x4_ceil_bits") (param i32) (result i32)
    local.get 0 f32.reinterpret_i32 f32x4.splat f32x4.ceil
    f32x4.extract_lane 0 i32.reinterpret_f32)
  (func (export "f32x4_floor_bits") (param i32) (result i32)
    local.get 0 f32.reinterpret_i32 f32x4.splat f32x4.floor
    f32x4.extract_lane 0 i32.reinterpret_f32)
  (func (export "f32x4_trunc_bits") (param i32) (result i32)
    local.get 0 f32.reinterpret_i32 f32x4.splat f32x4.trunc
    f32x4.extract_lane 0 i32.reinterpret_f32)
  (func (export "f32x4_sqrt_bits") (param i32) (result i32)
    local.get 0 f32.reinterpret_i32 f32x4.splat f32x4.sqrt
    f32x4.extract_lane 0 i32.reinterpret_f32)

  (func (export "f64x2_ceil_bits") (param i64) (result i64)
    local.get 0 f64.reinterpret_i64 f64x2.splat f64x2.ceil
    f64x2.extract_lane 0 i64.reinterpret_f64)
  (func (export "f64x2_floor_bits") (param i64) (result i64)
    local.get 0 f64.reinterpret_i64 f64x2.splat f64x2.floor
    f64x2.extract_lane 0 i64.reinterpret_f64)
  (func (export "f64x2_trunc_bits") (param i64) (result i64)
    local.get 0 f64.reinterpret_i64 f64x2.splat f64x2.trunc
    f64x2.extract_lane 0 i64.reinterpret_f64)
  (func (export "f64x2_sqrt_bits") (param i64) (result i64)
    local.get 0 f64.reinterpret_i64 f64x2.splat f64x2.sqrt
    f64x2.extract_lane 0 i64.reinterpret_f64))
