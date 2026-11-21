(module
  (func $target (result i32)
    i32.const 42)
  (elem declare func $target)

  (func (export "null_funcref") (result funcref)
    ref.null func)
  (func (export "null_externref") (result externref)
    ref.null extern)
  (func (export "is_null_funcref") (result i32)
    ref.null func
    ref.is_null)
  (func (export "is_null_externref") (result i32)
    ref.null extern
    ref.is_null)
  (func (export "function_handle") (result funcref)
    ref.func $target)
  (func (export "nonnull_funcref") (param $ref funcref) (result funcref)
    local.get $ref
    ref.as_non_null)
  (func (export "nonnull_externref") (param $ref externref) (result externref)
    local.get $ref
    ref.as_non_null))
