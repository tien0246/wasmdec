(module
  (func (export "shuffle") (param i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    i8x16.shuffle 0 17 2 19 4 21 6 23 8 25 10 27 12 29 14 31
    i8x16.extract_lane_u 15)
  (func (export "shuffle_reverse") (result i32)
    v128.const i32x4 0x03020100 0x07060504 0x0b0a0908 0x0f0e0d0c
    v128.const i32x4 0x13121110 0x17161514 0x1b1a1918 0x1f1e1d1c
    i8x16.shuffle 31 30 29 28 27 26 25 24 23 22 21 20 19 18 17 16
    i8x16.extract_lane_u 0)
  (func (export "replace_i32") (param i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1
    i32x4.replace_lane 3
    i32x4.extract_lane 3)
  (func (export "replace_i64") (param i64 i64) (result i64)
    local.get 0 i64x2.splat
    local.get 1
    i64x2.replace_lane 1
    i64x2.extract_lane 1)
  (func (export "replace_f32") (param f32 f32) (result i32)
    local.get 0 f32x4.splat
    local.get 1
    f32x4.replace_lane 2
    f32x4.extract_lane 2
    i32.reinterpret_f32)
  (func (export "replace_f64") (param f64 f64) (result i64)
    local.get 0 f64x2.splat
    local.get 1
    f64x2.replace_lane 1
    f64x2.extract_lane 1
    i64.reinterpret_f64))
