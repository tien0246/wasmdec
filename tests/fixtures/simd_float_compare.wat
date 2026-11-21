(module
  (func (export "f32_eq") (param f32 f32) (result i32)
    local.get 0 f32x4.splat
    local.get 1 f32x4.splat
    f32x4.eq
    i32x4.extract_lane 3)
  (func (export "f32_ne") (param f32 f32) (result i32)
    local.get 0 f32x4.splat
    local.get 1 f32x4.splat
    f32x4.ne
    i32x4.extract_lane 3)
  (func (export "f32_lt") (param f32 f32) (result i32)
    local.get 0 f32x4.splat
    local.get 1 f32x4.splat
    f32x4.lt
    i32x4.extract_lane 3)
  (func (export "f32_gt") (param f32 f32) (result i32)
    local.get 0 f32x4.splat
    local.get 1 f32x4.splat
    f32x4.gt
    i32x4.extract_lane 3)
  (func (export "f32_le") (param f32 f32) (result i32)
    local.get 0 f32x4.splat
    local.get 1 f32x4.splat
    f32x4.le
    i32x4.extract_lane 3)
  (func (export "f32_ge") (param f32 f32) (result i32)
    local.get 0 f32x4.splat
    local.get 1 f32x4.splat
    f32x4.ge
    i32x4.extract_lane 3)
  (func (export "f64_eq") (param f64 f64) (result i64)
    local.get 0 f64x2.splat
    local.get 1 f64x2.splat
    f64x2.eq
    i64x2.extract_lane 1)
  (func (export "f64_ne") (param f64 f64) (result i64)
    local.get 0 f64x2.splat
    local.get 1 f64x2.splat
    f64x2.ne
    i64x2.extract_lane 1)
  (func (export "f64_lt") (param f64 f64) (result i64)
    local.get 0 f64x2.splat
    local.get 1 f64x2.splat
    f64x2.lt
    i64x2.extract_lane 1)
  (func (export "f64_gt") (param f64 f64) (result i64)
    local.get 0 f64x2.splat
    local.get 1 f64x2.splat
    f64x2.gt
    i64x2.extract_lane 1)
  (func (export "f64_le") (param f64 f64) (result i64)
    local.get 0 f64x2.splat
    local.get 1 f64x2.splat
    f64x2.le
    i64x2.extract_lane 1)
  (func (export "f64_ge") (param f64 f64) (result i64)
    local.get 0 f64x2.splat
    local.get 1 f64x2.splat
    f64x2.ge
    i64x2.extract_lane 1))
