(module
  (memory 1)
  (func (export "load8x8_s") (param i32) (result i32)
    local.get 0
    v128.load8x8_s offset=3 align=1
    i16x8.extract_lane_s 7)
  (func (export "load8x8_u") (param i32) (result i32)
    local.get 0
    v128.load8x8_u offset=5 align=8
    i16x8.extract_lane_u 7)
  (func (export "load16x4_s") (param i32) (result i32)
    local.get 0
    v128.load16x4_s offset=7 align=2
    i32x4.extract_lane 3)
  (func (export "load16x4_u") (param i32) (result i32)
    local.get 0
    v128.load16x4_u offset=9 align=8
    i32x4.extract_lane 3)
  (func (export "load32x2_s") (param i32) (result i64)
    local.get 0
    v128.load32x2_s offset=11 align=1
    i64x2.extract_lane 1)
  (func (export "load32x2_u") (param i32) (result i64)
    local.get 0
    v128.load32x2_u offset=4294967295 align=8
    i64x2.extract_lane 1))
