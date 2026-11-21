(module
  (import "env" "forward_i32" (func $import_forward_i32 (param i32) (result i32)))

  (func $target_i32 (param i32) (result i32)
    local.get 0)
  (func (export "tail_i32") (param i32) (result i32)
    local.get 0
    return_call $target_i32)

  (func $target_i64 (param i64) (result i64)
    local.get 0)
  (func (export "tail_i64") (param i64) (result i64)
    local.get 0
    return_call $target_i64)

  (func $target_void (param i32))
  (func (export "tail_void") (param i32)
    local.get 0
    return_call $target_void)

  (func (export "tail_import") (param i32) (result i32)
    local.get 0
    return_call $import_forward_i32))
