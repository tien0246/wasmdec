(module
  (memory i64 1)
  (func (export "load64") (param i64) (result i32)
    local.get 0
    i32.load offset=4294967296
  )
)
