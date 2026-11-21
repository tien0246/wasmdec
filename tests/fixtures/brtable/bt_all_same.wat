(module
  (func (export "dispatch") (param i32) (result i32)
    (block $one
      local.get 0
      (br_table $one $one $one $one $one $one $one $one $one)
    )
    i32.const 42))
