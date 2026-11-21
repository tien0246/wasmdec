(module
  (type $sig (func (param i64) (result i32)))
  (import "env" "shared" (table $shared 2 (ref null $sig)))
  (func $consumer (type $sig) (param i64) (result i32)
    i32.const 0))
