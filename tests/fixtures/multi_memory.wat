(module
  (memory 1)
  (memory 1)
  (func (export "load1") (param i32) (result i32)
    local.get 0
    i32.load (memory 1)
  )
)
