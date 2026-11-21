;; V1 (Root-approved 08/11): green boundary matrix for the SIMD lane/
;; shuffle/align bounds modeled by the typed function validator
;; (core/function_validator.cpp step_simd + core/simd_validator.hpp).
;; Every function uses exactly the boundary value lane == bound-1
;; (i8x16=15, i16x8=7, i32x4/f32x4=3, i64x2/f64x2=1), shuffle index 31,
;; memarg align == natural width, and the empty-spec-slot neighbours
;; (0x60/0x61/0x62 unary) that must stay well-formed. The matching black
;; module is simd_lane_bounds_invalid.wasm (assembled byte-wise by
;; tests/fixtures/gen/make_simd_lane_bounds.py — wat2wasm refuses to
;; encode out-of-range immediates).
(module
  (memory 1)
  (func (export "load8_lane_15") (param i32)
    local.get 0
    v128.const i32x4 0 0 0 0
    v128.load8_lane offset=0
    15
    drop)
  (func (export "load16_lane_7") (param i32)
    local.get 0
    v128.const i32x4 0 0 0 0
    v128.load16_lane offset=0
    7
    drop)
  (func (export "load32_lane_3") (param i32)
    local.get 0
    v128.const i32x4 0 0 0 0
    v128.load32_lane offset=0
    3
    drop)
  (func (export "load64_lane_1") (param i32)
    local.get 0
    v128.const i32x4 0 0 0 0
    v128.load64_lane offset=0
    1
    drop)
  (func (export "store8_lane_15") (param i32)
    local.get 0 v128.const i32x4 0 0 0 0 v128.store8_lane offset=0
    15)
  (func (export "store16_lane_7") (param i32)
    local.get 0 v128.const i32x4 0 0 0 0 v128.store16_lane offset=0
    7)
  (func (export "store32_lane_3") (param i32)
    local.get 0 v128.const i32x4 0 0 0 0 v128.store32_lane offset=0
    3)
  (func (export "store64_lane_1") (param i32)
    local.get 0 v128.const i32x4 0 0 0 0 v128.store64_lane offset=0
    1)
  (func (export "extract_i8x16_15") (result i32)
    v128.const i32x4 0 0 0 0 i8x16.extract_lane_s 15)
  (func (export "extract_i16x8_7") (result i32)
    v128.const i32x4 0 0 0 0 i16x8.extract_lane_s 7)
  (func (export "extract_i32x4_3") (result i32)
    v128.const i32x4 0 0 0 0 i32x4.extract_lane 3)
  (func (export "extract_i64x2_1")
    v128.const i32x4 0 0 0 0 i64x2.extract_lane 1 drop)
  (func (export "extract_f32x4_3")
    v128.const i32x4 0 0 0 0 f32x4.extract_lane 3 drop)
  (func (export "extract_f64x2_1")
    v128.const i32x4 0 0 0 0 f64x2.extract_lane 1 drop)
  (func (export "replace_i8x16_15")
    v128.const i32x4 0 0 0 0 i32.const 0 i8x16.replace_lane 15 drop)
  (func (export "replace_i16x8_7")
    v128.const i32x4 0 0 0 0 i32.const 0 i16x8.replace_lane 7 drop)
  (func (export "replace_i32x4_3")
    v128.const i32x4 0 0 0 0 i32.const 0 i32x4.replace_lane 3 drop)
  (func (export "replace_i64x2_1")
    v128.const i32x4 0 0 0 0 i64.const 0 i64x2.replace_lane 1 drop)
  (func (export "replace_f32x4_3")
    v128.const i32x4 0 0 0 0 f32.const 0 f32x4.replace_lane 3 drop)
  (func (export "replace_f64x2_1")
    v128.const i32x4 0 0 0 0 f64.const 0 f64x2.replace_lane 1 drop)
  (func (export "shuffle_31")
    v128.const i32x4 0 0 0 0 v128.const i32x4 0 0 0 0
    i8x16.shuffle 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 31 drop)
  ;; align == natural width for every access width
  (func (export "align_v128_load_4") (param i32)
    local.get 0
    v128.load offset=0
    drop)
  (func (export "align_load8_lane_0") (param i32)
    local.get 0
    v128.const i32x4 0 0 0 0
    v128.load8_lane offset=0
    0
    drop)
  (func (export "align_load64_lane_3") (param i32)
    local.get 0
    v128.const i32x4 0 0 0 0
    v128.load64_lane offset=0
    0
    drop)
  (func (export "align_load32_zero_2") (param i32)
    local.get 0
    v128.load32_zero offset=0
    drop)
  ;; the neighbours of the empty spec slots stay well-formed unary ops
  (func (export "unary_0x60_abs")
    v128.const i32x4 0 0 0 0 i8x16.abs drop)
  (func (export "unary_0x61_neg")
    v128.const i32x4 0 0 0 0 i8x16.neg drop)
  (func (export "unary_0x62_popcnt")
    v128.const i32x4 0 0 0 0 i8x16.popcnt drop))
