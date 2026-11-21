(module
  (func (export "explicit_return") (param i32) (result i32)
    local.get 0
    return
    i32.const 99))
