(module
  ;; Same gojs import signal, but the byte-level evidence is incomplete:
  ;; only 4 globals (Go needs 8 SP/CTXT/g/RET0-3/PAUSE slots) — the
  ;; provenance scan must refuse the 'G' flag.
  (import "gojs" "runtime.wasmExit" (func $wasmExit (param i32)))
  (global $sp (mut i32) (i32.const 0))
  (global $ctxt (mut i32) (i32.const 0))
  (global $g (mut i32) (i32.const 0))
  (global $ret0 (mut i32) (i32.const 0))

  (func $go_a (param $pc i32) (result i32)
    global.get 0
    i32.const 16
    i32.sub
    global.set 0
    local.get $pc
    i32.const 1
    i32.add
    global.get 0
    i32.const 16
    i32.add
    global.set 0)
  (func $go_b (param $pc i32) (result i32)
    global.get 0
    i32.const 32
    i32.sub
    global.set 0
    local.get $pc
    i32.const 2
    i32.mul
    global.get 0
    i32.const 32
    i32.add
    global.set 0)
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
  (func (export "entry") (param $pc i32) (result i32)
    local.get $pc
    call $go_a)
)
