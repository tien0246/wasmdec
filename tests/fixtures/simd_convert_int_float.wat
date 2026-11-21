(module
  (func (export "f32_from_i32_s") (param i32) (result i32)
    local.get 0 i32x4.splat
    f32x4.convert_i32x4_s
    f32x4.extract_lane 3
    i32.reinterpret_f32)
  (func (export "f32_from_i32_u") (param i32) (result i32)
    local.get 0 i32x4.splat
    f32x4.convert_i32x4_u
    f32x4.extract_lane 3
    i32.reinterpret_f32)
  (func (export "f64_from_i32_s") (param i32) (result i64)
    local.get 0 i32x4.splat
    f64x2.convert_low_i32x4_s
    f64x2.extract_lane 1
    i64.reinterpret_f64)
  (func (export "f64_from_i32_u") (param i32) (result i64)
    local.get 0 i32x4.splat
    f64x2.convert_low_i32x4_u
    f64x2.extract_lane 1
    i64.reinterpret_f64))
