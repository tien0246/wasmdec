(module
  (memory 0)
  (func (export "load") (param i32) (result i32)
    local.get 0 i32.load8_s)
)
