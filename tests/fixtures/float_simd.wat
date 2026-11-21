(module
  (func (export "f32_roundtrip") (param f32) (result f32)
    local.get 0
    f32x4.splat
    f32x4.extract_lane 3)
  (func (export "f32_add") (param f32 f32) (result f32)
    local.get 0
    f32x4.splat
    local.get 1
    f32x4.splat
    f32x4.add
    f32x4.extract_lane 2)
  (func (export "f32_sub") (param f32 f32) (result f32)
    local.get 0
    f32x4.splat
    local.get 1
    f32x4.splat
    f32x4.sub
    f32x4.extract_lane 1)
  (func (export "f32_mul") (param f32 f32) (result f32)
    local.get 0
    f32x4.splat
    local.get 1
    f32x4.splat
    f32x4.mul
    f32x4.extract_lane 0)
  (func (export "f32_div") (param f32 f32) (result f32)
    local.get 0
    f32x4.splat
    local.get 1
    f32x4.splat
    f32x4.div
    f32x4.extract_lane 3)
  (func (export "f32_abs_bits") (param f32) (result i32)
    local.get 0
    f32x4.splat
    f32x4.abs
    f32x4.extract_lane 0
    i32.reinterpret_f32)
  (func (export "f32_neg_bits") (param f32) (result i32)
    local.get 0
    f32x4.splat
    f32x4.neg
    f32x4.extract_lane 1
    i32.reinterpret_f32)
  (func (export "f64_roundtrip") (param f64) (result f64)
    local.get 0
    f64x2.splat
    f64x2.extract_lane 1)
  (func (export "f64_add") (param f64 f64) (result f64)
    local.get 0
    f64x2.splat
    local.get 1
    f64x2.splat
    f64x2.add
    f64x2.extract_lane 1)
  (func (export "f64_sub") (param f64 f64) (result f64)
    local.get 0
    f64x2.splat
    local.get 1
    f64x2.splat
    f64x2.sub
    f64x2.extract_lane 0)
  (func (export "f64_mul") (param f64 f64) (result f64)
    local.get 0
    f64x2.splat
    local.get 1
    f64x2.splat
    f64x2.mul
    f64x2.extract_lane 1)
  (func (export "f64_div") (param f64 f64) (result f64)
    local.get 0
    f64x2.splat
    local.get 1
    f64x2.splat
    f64x2.div
    f64x2.extract_lane 0)
  (func (export "f64_abs_bits") (param f64) (result i64)
    local.get 0
    f64x2.splat
    f64x2.abs
    f64x2.extract_lane 0
    i64.reinterpret_f64)
  (func (export "f64_neg_bits") (param f64) (result i64)
    local.get 0
    f64x2.splat
    f64x2.neg
    f64x2.extract_lane 1
    i64.reinterpret_f64))
