(module
  (func (export "f32_nearest_bits") (param i32) (result i32)
    local.get 0 f32.reinterpret_i32 f32.nearest i32.reinterpret_f32)
  (func (export "f64_nearest_bits") (param i64) (result i64)
    local.get 0 f64.reinterpret_i64 f64.nearest i64.reinterpret_f64))
