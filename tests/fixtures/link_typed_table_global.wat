(module
  (type $sig (func (param i32) (result i32)))
  (import "env" "slot" (global (ref null $sig)))
  (table $shared 1 (ref null $sig))
  (elem (table $shared) (i32.const 0) (ref null $sig) (global.get 0)))
