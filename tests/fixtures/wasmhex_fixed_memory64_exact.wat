(module
  (import "env" "memory" (memory i64 1 1 shared))
  (export "memory" (memory 0))
  (func (export "load_scalar") (param $a i64) (result i32)
    local.get $a
    i32.load)
  (func (export "store_scalar") (param $a i64) (param $v i32)
    local.get $a
    local.get $v
    i32.store)
  (func (export "load_v128") (param $a i64) (result i32)
    local.get $a
    v128.load
    drop
    i32.const 1)
  (func (export "load_lane") (param $a i64) (result i32)
    local.get $a
    i64.const 0
    v128.load
    v128.load8_lane 0
    drop
    i32.const 2)
  (func (export "load_widen") (param $a i64) (result i32)
    local.get $a v128.load8x8_s drop
    local.get $a v128.load8x8_u drop
    local.get $a v128.load16x4_s drop
    local.get $a v128.load16x4_u drop
    local.get $a v128.load32x2_s drop
    local.get $a v128.load32x2_u drop
    i32.const 3)
  (func (export "try_grow") (param $delta i64) (result i64)
    local.get $delta
    memory.grow))
