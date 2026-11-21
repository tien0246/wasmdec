(module
  (func (export "relaxed_swizzle") (param i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat i8x16.relaxed_swizzle local.set 2
    local.get 2 i64x2.extract_lane 0
    local.get 2 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_trunc_f32_s") (param i64) (result i64) (local v128)
    local.get 0 i64x2.splat i32x4.relaxed_trunc_f32x4_s local.set 1
    local.get 1 i64x2.extract_lane 0
    local.get 1 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_trunc_f32_u") (param i64) (result i64) (local v128)
    local.get 0 i64x2.splat i32x4.relaxed_trunc_f32x4_u local.set 1
    local.get 1 i64x2.extract_lane 0
    local.get 1 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_trunc_f64_s_zero") (param i64) (result i64) (local v128)
    local.get 0 i64x2.splat i32x4.relaxed_trunc_f64x2_s_zero local.set 1
    local.get 1 i64x2.extract_lane 0
    local.get 1 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_trunc_f64_u_zero") (param i64) (result i64) (local v128)
    local.get 0 i64x2.splat i32x4.relaxed_trunc_f64x2_u_zero local.set 1
    local.get 1 i64x2.extract_lane 0
    local.get 1 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_f32_madd") (param i64 i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat local.get 2 i64x2.splat f32x4.relaxed_madd local.set 3
    local.get 3 i64x2.extract_lane 0
    local.get 3 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_f32_nmadd") (param i64 i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat local.get 2 i64x2.splat f32x4.relaxed_nmadd local.set 3
    local.get 3 i64x2.extract_lane 0
    local.get 3 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_f64_madd") (param i64 i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat local.get 2 i64x2.splat f64x2.relaxed_madd local.set 3
    local.get 3 i64x2.extract_lane 0
    local.get 3 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_f64_nmadd") (param i64 i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat local.get 2 i64x2.splat f64x2.relaxed_nmadd local.set 3
    local.get 3 i64x2.extract_lane 0
    local.get 3 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_laneselect_i8") (param i64 i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat local.get 2 i64x2.splat i8x16.relaxed_laneselect local.set 3
    local.get 3 i64x2.extract_lane 0
    local.get 3 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_laneselect_i16") (param i64 i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat local.get 2 i64x2.splat i16x8.relaxed_laneselect local.set 3
    local.get 3 i64x2.extract_lane 0
    local.get 3 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_laneselect_i32") (param i64 i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat local.get 2 i64x2.splat i32x4.relaxed_laneselect local.set 3
    local.get 3 i64x2.extract_lane 0
    local.get 3 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_laneselect_i64") (param i64 i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat local.get 2 i64x2.splat i64x2.relaxed_laneselect local.set 3
    local.get 3 i64x2.extract_lane 0
    local.get 3 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_f32_min") (param i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat f32x4.relaxed_min local.set 2
    local.get 2 i64x2.extract_lane 0
    local.get 2 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_f32_max") (param i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat f32x4.relaxed_max local.set 2
    local.get 2 i64x2.extract_lane 0
    local.get 2 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_f64_min") (param i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat f64x2.relaxed_min local.set 2
    local.get 2 i64x2.extract_lane 0
    local.get 2 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_f64_max") (param i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat f64x2.relaxed_max local.set 2
    local.get 2 i64x2.extract_lane 0
    local.get 2 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_q15mulr") (param i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat i16x8.relaxed_q15mulr_s local.set 2
    local.get 2 i64x2.extract_lane 0
    local.get 2 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_dot_i8_i7") (param i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat i16x8.relaxed_dot_i8x16_i7x16_s local.set 2
    local.get 2 i64x2.extract_lane 0
    local.get 2 i64x2.extract_lane 1 i64.add)
  (func (export "relaxed_dot_i8_i7_add") (param i64 i64 i64) (result i64) (local v128)
    local.get 0 i64x2.splat local.get 1 i64x2.splat local.get 2 i64x2.splat i32x4.relaxed_dot_i8x16_i7x16_add_s local.set 3
    local.get 3 i64x2.extract_lane 0
    local.get 3 i64x2.extract_lane 1 i64.add)
)
