(module
  (type $br_null_sig (func (param i32 i64 externref) (result i32 i64)))
  (type $br_non_null_sig
    (func (param i32 i64 externref) (result i32 i64 externref)))

  (func $select_ref (param i32 i64 externref) (result externref)
    local.get 2)

  (func (export "br_on_null_block_params")
      (param i32 i64 externref) (result i32 i64)
    local.get 0
    local.get 1
    local.get 2
    (block $done (type $br_null_sig)
      br_on_null $done
      drop
      drop
      drop
      i32.const 11
      i64.const 21))

  (func (export "br_on_non_null_block_params")
      (param i32 i64 externref externref) (result externref)
    local.get 0
    local.get 1
    local.get 2
    (block $done (type $br_non_null_sig)
      br_on_non_null $done
      local.get 3)
    call $select_ref))
