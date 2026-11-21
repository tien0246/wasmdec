(module
  ;; Arithmetic wrappers accept and return raw bits.
  (func (export "f32_add_bits") (param i32 i32) (result i32)
    local.get 0 f32.reinterpret_i32
    local.get 1 f32.reinterpret_i32
    f32.add i32.reinterpret_f32)
  (func (export "f32_sub_bits") (param i32 i32) (result i32)
    local.get 0 f32.reinterpret_i32
    local.get 1 f32.reinterpret_i32
    f32.sub i32.reinterpret_f32)
  (func (export "f32_mul_bits") (param i32 i32) (result i32)
    local.get 0 f32.reinterpret_i32
    local.get 1 f32.reinterpret_i32
    f32.mul i32.reinterpret_f32)
  (func (export "f32_div_bits") (param i32 i32) (result i32)
    local.get 0 f32.reinterpret_i32
    local.get 1 f32.reinterpret_i32
    f32.div i32.reinterpret_f32)
  (func (export "f64_add_bits") (param i64 i64) (result i64)
    local.get 0 f64.reinterpret_i64
    local.get 1 f64.reinterpret_i64
    f64.add i64.reinterpret_f64)
  (func (export "f64_sub_bits") (param i64 i64) (result i64)
    local.get 0 f64.reinterpret_i64
    local.get 1 f64.reinterpret_i64
    f64.sub i64.reinterpret_f64)
  (func (export "f64_mul_bits") (param i64 i64) (result i64)
    local.get 0 f64.reinterpret_i64
    local.get 1 f64.reinterpret_i64
    f64.mul i64.reinterpret_f64)
  (func (export "f64_div_bits") (param i64 i64) (result i64)
    local.get 0 f64.reinterpret_i64
    local.get 1 f64.reinterpret_i64
    f64.div i64.reinterpret_f64)

  ;; Integer-to-float and width-conversion outputs are returned as raw bits.
  (func (export "f32_from_i32_s_bits") (param i32) (result i32)
    local.get 0 f32.convert_i32_s i32.reinterpret_f32)
  (func (export "f32_from_i32_u_bits") (param i32) (result i32)
    local.get 0 f32.convert_i32_u i32.reinterpret_f32)
  (func (export "f32_from_i64_s_bits") (param i64) (result i32)
    local.get 0 f32.convert_i64_s i32.reinterpret_f32)
  (func (export "f32_from_i64_u_bits") (param i64) (result i32)
    local.get 0 f32.convert_i64_u i32.reinterpret_f32)
  (func (export "f64_from_i32_s_bits") (param i32) (result i64)
    local.get 0 f64.convert_i32_s i64.reinterpret_f64)
  (func (export "f64_from_i32_u_bits") (param i32) (result i64)
    local.get 0 f64.convert_i32_u i64.reinterpret_f64)
  (func (export "f64_from_i64_s_bits") (param i64) (result i64)
    local.get 0 f64.convert_i64_s i64.reinterpret_f64)
  (func (export "f64_from_i64_u_bits") (param i64) (result i64)
    local.get 0 f64.convert_i64_u i64.reinterpret_f64)
  (func (export "f32_demote_f64_bits") (param i64) (result i32)
    local.get 0 f64.reinterpret_i64 f32.demote_f64 i32.reinterpret_f32)
  (func (export "f64_promote_f32_bits") (param i32) (result i64)
    local.get 0 f32.reinterpret_i32 f64.promote_f32 i64.reinterpret_f64)

  ;; Trapping float-to-integer conversions.
  (func (export "i32_trunc_f32_s_bits") (param i32) (result i32)
    local.get 0 f32.reinterpret_i32 i32.trunc_f32_s)
  (func (export "i32_trunc_f32_u_bits") (param i32) (result i32)
    local.get 0 f32.reinterpret_i32 i32.trunc_f32_u)
  (func (export "i32_trunc_f64_s_bits") (param i64) (result i32)
    local.get 0 f64.reinterpret_i64 i32.trunc_f64_s)
  (func (export "i32_trunc_f64_u_bits") (param i64) (result i32)
    local.get 0 f64.reinterpret_i64 i32.trunc_f64_u)
  (func (export "i64_trunc_f32_s_bits") (param i32) (result i64)
    local.get 0 f32.reinterpret_i32 i64.trunc_f32_s)
  (func (export "i64_trunc_f32_u_bits") (param i32) (result i64)
    local.get 0 f32.reinterpret_i32 i64.trunc_f32_u)
  (func (export "i64_trunc_f64_s_bits") (param i64) (result i64)
    local.get 0 f64.reinterpret_i64 i64.trunc_f64_s)
  (func (export "i64_trunc_f64_u_bits") (param i64) (result i64)
    local.get 0 f64.reinterpret_i64 i64.trunc_f64_u)

  ;; Saturating float-to-integer conversions.
  (func (export "i32_sat_f32_s_bits") (param i32) (result i32)
    local.get 0 f32.reinterpret_i32 i32.trunc_sat_f32_s)
  (func (export "i32_sat_f32_u_bits") (param i32) (result i32)
    local.get 0 f32.reinterpret_i32 i32.trunc_sat_f32_u)
  (func (export "i32_sat_f64_s_bits") (param i64) (result i32)
    local.get 0 f64.reinterpret_i64 i32.trunc_sat_f64_s)
  (func (export "i32_sat_f64_u_bits") (param i64) (result i32)
    local.get 0 f64.reinterpret_i64 i32.trunc_sat_f64_u)
  (func (export "i64_sat_f32_s_bits") (param i32) (result i64)
    local.get 0 f32.reinterpret_i32 i64.trunc_sat_f32_s)
  (func (export "i64_sat_f32_u_bits") (param i32) (result i64)
    local.get 0 f32.reinterpret_i32 i64.trunc_sat_f32_u)
  (func (export "i64_sat_f64_s_bits") (param i64) (result i64)
    local.get 0 f64.reinterpret_i64 i64.trunc_sat_f64_s)
  (func (export "i64_sat_f64_u_bits") (param i64) (result i64)
    local.get 0 f64.reinterpret_i64 i64.trunc_sat_f64_u)
)
