(module
  (import "env" "external" (global $external i32))
  (import "env" "shared" (global $shared i32))
  (global $copy i32 (global.get $external))
  (func $read_external (export "read_external") (result i32) (global.get $external))
  (func $read_shared (export "read_shared") (result i32) (global.get $shared))
  (func $read_copy (export "read_copy") (result i32) (global.get $copy))
)
