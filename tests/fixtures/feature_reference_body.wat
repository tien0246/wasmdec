(module
  (func (export "is_null") (result i32)
    ref.null func
    ref.is_null)
  (func (export "typed_select") (param i32) (result i32)
    i32.const 1
    i32.const 2
    local.get 0
    select (result i32)))
