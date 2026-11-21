(module
  (type $sig (func (param i32) (result i32)))
  (table $shared 1 (ref null $sig))
  (func $target (type $sig) (param i32) (result i32)
    local.get 0)
  (elem (table $shared) (i32.const 0) (ref null $sig) (ref.func $target)))
