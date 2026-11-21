(module
  (func (export "f32_abs_bits") (param i32) (result i32)
    local.get 0
    f32.reinterpret_i32
    f32.abs
    i32.reinterpret_f32)
  (func (export "f32_neg_bits") (param i32) (result i32)
    local.get 0
    f32.reinterpret_i32
    f32.neg
    i32.reinterpret_f32)
  (func (export "f32_copysign_bits") (param i32 i32) (result i32)
    local.get 0
    f32.reinterpret_i32
    local.get 1
    f32.reinterpret_i32
    f32.copysign
    i32.reinterpret_f32)
  (func (export "f64_abs_bits") (param i64) (result i64)
    local.get 0
    f64.reinterpret_i64
    f64.abs
    i64.reinterpret_f64)
  (func (export "f64_neg_bits") (param i64) (result i64)
    local.get 0
    f64.reinterpret_i64
    f64.neg
    i64.reinterpret_f64)
  (func (export "f64_copysign_bits") (param i64 i64) (result i64)
    local.get 0
    f64.reinterpret_i64
    local.get 1
    f64.reinterpret_i64
    f64.copysign
    i64.reinterpret_f64))
