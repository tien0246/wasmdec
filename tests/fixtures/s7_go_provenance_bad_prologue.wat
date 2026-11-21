(module
  ;; Same gojs import signal + full compiler global layout, but the
  ;; functions never open with an SP prologue — provenance must refuse.
  (import "gojs" "runtime.wasmExit" (func $wasmExit (param i32)))
  (global $sp (mut i32) (i32.const 0))
  (global $ctxt (mut i64) (i64.const 0))
  (global $g (mut i64) (i64.const 0))
  (global $ret0 (mut i64) (i64.const 0))
  (global $ret1 (mut i64) (i64.const 0))
  (global $ret2 (mut i64) (i64.const 0))
  (global $ret3 (mut i64) (i64.const 0))
  (global $pause (mut i32) (i32.const 0))

  (func $no_prologue_a (param $pc i32) (result i32)
    local.get $pc
    i32.const 1
    i32.add)
  (func $no_prologue_b (param $pc i32) (result i32)
    local.get $pc
    i32.const 2
    i32.mul)
  (func $no_prologue_c (param $pc i32) (result i32)
    local.get $pc
    i32.const 3
    i32.sub)
  (func (export "entry") (param $pc i32) (result i32)
    local.get $pc
    call $no_prologue_a)
)
