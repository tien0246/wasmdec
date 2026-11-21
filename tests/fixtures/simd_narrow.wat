(module
  (func (export "narrow_i16_to_i8_s") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i8x16.narrow_i16x8_s
    i8x16.extract_lane_s 15)
  (func (export "narrow_i16_to_i8_u") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i8x16.narrow_i16x8_u
    i8x16.extract_lane_u 15)
  (func (export "narrow_i32_to_i16_s") (param i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    i16x8.narrow_i32x4_s
    i16x8.extract_lane_s 7)
  (func (export "narrow_i32_to_i16_u") (param i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    i16x8.narrow_i32x4_u
    i16x8.extract_lane_u 7))
