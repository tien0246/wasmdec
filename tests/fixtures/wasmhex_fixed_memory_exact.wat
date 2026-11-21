(module
  (import "env" "memory" (memory 1 1 shared))
  (export "memory" (memory 0))
  (func (export "load_scalar") (param i32) (result i32)
    local.get 0 i32.load)
  (func (export "store_scalar") (param i32 i32)
    local.get 0 local.get 1 i32.store)
  (func (export "load_v128") (param i32) (result i32)
    local.get 0 v128.load drop i32.const 1)
  (func (export "load_lane") (param i32) (result i32)
    local.get 0
    i32.const 0
    v128.load
    v128.load8_lane 0
    drop
    i32.const 2)
  (func (export "load_widen") (param i32) (result i32)
    local.get 0 v128.load8x8_s drop
    local.get 0 v128.load8x8_u drop
    local.get 0 v128.load16x4_s drop
    local.get 0 v128.load16x4_u drop
    local.get 0 v128.load32x2_s drop
    local.get 0 v128.load32x2_u drop
    i32.const 3)
  (func (export "try_grow") (param i32) (result i32)
    local.get 0 memory.grow))
