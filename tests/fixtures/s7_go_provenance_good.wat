(module
  ;; Go-style runtime import (signal — never sufficient alone)
  (import "gojs" "runtime.wasmExit" (func $wasmExit (param i32)))
  ;; Globals 0..7 = SP, CTXT, g, RET0-3, PAUSE — the exact compiler layout:
  ;; i32, i64 x6, i32 (all mutable)
  (global $sp (mut i32) (i32.const 0))
  (global $ctxt (mut i64) (i64.const 0))
  (global $g (mut i64) (i64.const 0))
  (global $ret0 (mut i64) (i64.const 0))
  (global $ret1 (mut i64) (i64.const 0))
  (global $ret2 (mut i64) (i64.const 0))
  (global $ret3 (mut i64) (i64.const 0))
  (global $pause (mut i32) (i32.const 0))
  (global $stack_top i32 (i32.const 65536))

  ;; PC_B signature + SP-copy prologue (majority shape, as emitted by
  ;; cmd/internal/obj/wasm: global.get 0; local.set 1)
  (func $go_a (param $pc i32) (result i32)
    (local $sp_copy i32)
    global.get 0
    local.set 1
    local.get $pc
    i32.const 1
    i32.add)
  (func $go_b (param $pc i32) (result i32)
    (local $sp_copy i32)
    global.get 0
    local.set 1
    local.get $pc
    i32.const 2
    i32.mul)
  (func $go_c (param $pc i32) (result i32)
    global.get 0
    i32.const 16
    i32.sub
    global.set 0
    local.get $pc
    global.get 0
    i32.const 16
    i32.add
    global.set 0)
  ;; minority non-PC_B helper (like the notUsePC_B exceptions)
  (func $plain (param $a i32) (param $b i32) (result i32)
    local.get $a
    local.get $b
    i32.add)
  (func (export "entry") (param $pc i32) (result i32)
    local.get $pc
    call $go_a)
)
