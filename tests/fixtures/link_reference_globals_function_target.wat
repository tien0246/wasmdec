(module
  (func $target (export "target") (result i32) (i32.const 2))
  (global $target_ref (export "target_ref") funcref (ref.func $target))
)
