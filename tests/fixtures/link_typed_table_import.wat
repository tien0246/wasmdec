(module
  (type $sig (func (param i32) (result i32)))
  (import "env" "shared" (table $shared 2 (ref null $sig)))
  (func $consumer (export "consumer") (type $sig) (param i32) (result i32)
    local.get 0)
  (elem (table $shared) (i32.const 1) (ref null $sig) (ref.null 0))
  (elem (ref null $sig) (ref.null 0))
  (elem declare (ref null $sig) (ref.null 0)))
