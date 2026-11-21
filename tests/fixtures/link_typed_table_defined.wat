(module
  (type $prefix (func (param i64)))
  (type $sig (func (param i32) (result i32)))
  (table $shared (export "shared") 2 (ref null $sig))
  (func $producer (export "producer") (type $sig) (param i32) (result i32)
    local.get 0)
  (elem (table $shared) (i32.const 0) (ref null $sig) (ref.null 1)))
