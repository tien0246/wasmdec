(module
  (func $f32_add (export "f32_add") (param f32) (result f32)
    local.get 0
    f32.const 1
    f32.add)
  (func (export "f32_call") (param f32) (result f32)
    local.get 0
    call $f32_add)
  (func $f64_add (export "f64_add") (param f64) (result f64)
    local.get 0
    f64.const 1
    f64.add)
  (func (export "f64_call") (param f64) (result f64)
    local.get 0
    call $f64_add))
