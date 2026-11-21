(module
  (func (export "const_i16_lane7") (result i32)
    v128.const i32x4 0x11223344 0x55667788 0x99aabbcc 0xddeeff00
    i16x8.extract_lane_u 7)
  (func (export "i16_splat_to_i32") (param i32) (result i32)
    local.get 0 i16x8.splat
    i32x4.extract_lane 1)
  (func (export "i32_splat_to_i16") (param i32) (result i32)
    local.get 0 i32x4.splat
    i16x8.extract_lane_u 3)
  (func (export "i32_splat_to_i64") (param i32) (result i64)
    local.get 0 i32x4.splat
    i64x2.extract_lane 1)
  (func (export "i64_splat_to_i32") (param i64) (result i32)
    local.get 0 i64x2.splat
    i32x4.extract_lane 1)
  (func (export "i64_splat_to_i16") (param i64) (result i32)
    local.get 0 i64x2.splat
    i16x8.extract_lane_u 5)
  (func (export "i16_add_to_i32") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.add
    i32x4.extract_lane 2)
  (func (export "i16_replace_to_i32") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1
    i16x8.replace_lane 5
    i32x4.extract_lane 2)
  (func (export "i64_add_to_i32") (param i64 i64) (result i32)
    local.get 0 i64x2.splat
    local.get 1 i64x2.splat
    i64x2.add
    i32x4.extract_lane 3)
  (func (export "f64_splat_to_f32") (param f64) (result i32)
    local.get 0 f64x2.splat
    f32x4.extract_lane 2
    i32.reinterpret_f32)
  (func (export "f32_splat_to_f64") (param f32) (result i64)
    local.get 0 f32x4.splat
    f64x2.extract_lane 1
    i64.reinterpret_f64))
