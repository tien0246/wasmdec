(module
  (func (export "demote_lane1") (param f64) (result i32)
    local.get 0
    f64x2.splat
    f32x4.demote_f64x2_zero
    f32x4.extract_lane 1
    i32.reinterpret_f32)
  (func (export "demote_zero_lane3") (param f64) (result i32)
    local.get 0
    f64x2.splat
    f32x4.demote_f64x2_zero
    f32x4.extract_lane 3
    i32.reinterpret_f32)
  (func (export "promote_lane1") (param f32) (result i64)
    local.get 0
    f32x4.splat
    f64x2.promote_low_f32x4
    f64x2.extract_lane 1
    i64.reinterpret_f64)
  (func (export "promote_pattern_lane1") (result i64)
    v128.const i32x4 0x3f800000 0x40000000 0x40400000 0x40800000
    f64x2.promote_low_f32x4
    f64x2.extract_lane 1
    i64.reinterpret_f64))
