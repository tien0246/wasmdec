(module
  (import "env" "echo_ref" (func $echo_ref (param externref) (result funcref)))
  (import "env" "select_ref" (func $select_ref (param funcref externref) (result externref)))

  (func $pair32 (param i32) (result i32 i32)
    local.get 0
    i32.const 7)

  (func (export "wasm_call_pair32") (param i32) (result i32 i32)
    local.get 0
    call $pair32)

  (func $tuple6 (param i32) (result i32 i64 i32 i32 i32)
    local.get 0
    local.get 0
    i64.extend_i32_u
    i32.const 11
    i32.const 22
    i32.const 33)

  (func (export "wasm_call_tuple6") (param i32) (result i32 i64 i32 i32 i32)
    local.get 0
    call $tuple6)

  (func $mixed (result f32 i32 f64)
    f32.const 1.5
    i32.const 7
    f64.const 2.5)

  (func (export "wasm_call_mixed") (result f32 i32 f64)
    call $mixed)

  (func (export "wasm_ref_bridge") (param externref) (result funcref)
    local.get 0
    call $echo_ref)

  (func (export "wasm_ref_pair_bridge") (param funcref externref) (result externref)
    local.get 0
    local.get 1
    call $select_ref))
