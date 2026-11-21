(module
  (func $identity_i64 (param i64) (result i64)
    local.get 0)
  (func (export "direct_call_i64") (param i64) (result i64)
    local.get 0
    call $identity_i64)
  (func (export "conditional_return_i64") (param i32 i64) (result i64)
    local.get 1
    local.get 0
    br_if 0
    drop
    i64.const 7))
