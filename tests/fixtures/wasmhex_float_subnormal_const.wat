(module
  (func (export "subnormal_f32") (result f32)
    f32.const 0x1.p-149)
  (func (export "subnormal_f64") (result f64)
    f64.const 0x1.p-1074)
  (func (export "negative_subnormal_f64") (result f64)
    f64.const -0x1.p-1074)
)
