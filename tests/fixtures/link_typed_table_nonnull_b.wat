(module
  (type $sig (func (param i32) (result i32)))
  (import "env" "non_null" (table $shared 0 (ref $sig))))
