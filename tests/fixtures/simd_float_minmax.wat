(module
  (func (export "f32_min") (param f32 f32) (result f32)
    local.get 0 f32x4.splat local.get 1 f32x4.splat
    f32x4.min f32x4.extract_lane 0)
  (func (export "f32_max") (param f32 f32) (result f32)
    local.get 0 f32x4.splat local.get 1 f32x4.splat
    f32x4.max f32x4.extract_lane 0)
  (func (export "f32_pmin") (param f32 f32) (result f32)
    local.get 0 f32x4.splat local.get 1 f32x4.splat
    f32x4.pmin f32x4.extract_lane 0)
  (func (export "f32_pmax") (param f32 f32) (result f32)
    local.get 0 f32x4.splat local.get 1 f32x4.splat
    f32x4.pmax f32x4.extract_lane 0)
  (func (export "f64_min") (param f64 f64) (result f64)
    local.get 0 f64x2.splat local.get 1 f64x2.splat
    f64x2.min f64x2.extract_lane 0)
  (func (export "f64_max") (param f64 f64) (result f64)
    local.get 0 f64x2.splat local.get 1 f64x2.splat
    f64x2.max f64x2.extract_lane 0)
  (func (export "f64_pmin") (param f64 f64) (result f64)
    local.get 0 f64x2.splat local.get 1 f64x2.splat
    f64x2.pmin f64x2.extract_lane 0)
  (func (export "f64_pmax") (param f64 f64) (result f64)
    local.get 0 f64x2.splat local.get 1 f64x2.splat
    f64x2.pmax f64x2.extract_lane 0))
