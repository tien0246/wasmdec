(module
  (import "env" "same" (global $same (mut i32)))
  (func (export "read_imported") (result i32) (global.get $same))
)
