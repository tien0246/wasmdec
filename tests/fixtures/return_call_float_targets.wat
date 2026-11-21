(module
  (func $target_f32 (export "target_f32") (param f32) (result f32)
    local.get 0
    f32.const 1
    f32.add)
  (func (export "tail_f32") (param f32) (result f32)
    local.get 0
    return_call $target_f32)

  (func $target_f64 (export "target_f64") (param f64) (result f64)
    local.get 0
    f64.const 1
    f64.add)
  (func (export "tail_f64") (param f64) (result f64)
    local.get 0
    return_call $target_f64))
