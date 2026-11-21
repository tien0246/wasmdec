(module
  (type $sig (func (result i32)))
  (import "env" "choice" (func $choice (type $sig)))
  (func (export "run") (result i32) (call $choice))
)
