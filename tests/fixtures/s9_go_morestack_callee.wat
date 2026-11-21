(module
  ;; S9/K1: Go guard hiding on a nameless shell. The gojs import raises the
  ;; module signal; the 9-global compiler layout plus the PC_B/SP-prologue
  ;; majority below raise the byte-level Go provenance ('G' v1) that gates
  ;; the rewriter. Mirrors the go1.26.5 main.wasm shapes:
  ;;   guard:  local.get 1; global.get 2; i32.wrap; i32.load offset=0x10;
  ;;           i32.le_u; if ...
  ;;   then:   SP-=8; local.tee; global.set 0; *(u32*)SP = const;
  ;;           call <unnamed>; refresh SP        (bytes 20 01 41 08 6b 22
  ;;           01 24 00 ... - func 28 of main.wasm)
  (import "gojs" "runtime.wasmExit" (func $wasmExit (param i32)))
  ;; Globals 0..7 = SP, CTXT, g, RET0-3, PAUSE — the compiler layout:
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
  (memory 1)

  ;; The growth helper. Not exported, no name section entry: the loader
  ;; names it wasm_func_1 - the "unnamed Go shell callee" of S9. Takes the
  ;; single i32 the real growth call is issued with (41 00 10 ..).
  (func $growth (param i32))

  ;; S9 case 1: canonical unnamed-shell guard - MUST be hidden.
  (func (export "s9_guard_hidden") (param $pc i32) (result i32)
    (local $spc i32)
    global.get $sp
    local.set $spc
    local.get $spc
    global.get $g
    i32.wrap_i64
    i32.load offset=16
    i32.le_u
    if
      local.get $spc
      i32.const 8
      i32.sub
      local.tee $spc
      global.set $sp
      local.get $spc
      i32.const 4660
      i32.store
      i32.const 0
      call $growth
      global.get $sp
      local.set $spc
    end
    i32.const 42)

  ;; S9 case 2: plain SP-vs-global compare (no +0x10 deref of the guard
  ;; slot) with the same then-block - MUST be kept.
  (func (export "s9_plain_kept") (param $pc i32) (result i32)
    (local $spc i32)
    global.get $sp
    local.set $spc
    local.get $spc
    global.get $sp
    i32.le_u
    if
      local.get $spc
      i32.const 8
      i32.sub
      local.tee $spc
      global.set $sp
      local.get $spc
      i32.const 4660
      i32.store
      i32.const 0
      call $growth
      global.get $sp
      local.set $spc
    end
    i32.const 7)

  ;; S9 case 3 (differential): the callee NAME merely contains the
  ;; synthetic prefix. The pre-S9 substring check accepted "xwasm_func_9"
  ;; as a growth call and hid this guard; the prefix check must not.
  (func $xwasm (export "xwasm_func_9") (param i32))
  (func (export "s9_infix_callee_kept") (param $pc i32) (result i32)
    (local $spc i32)
    global.get $sp
    local.set $spc
    local.get $spc
    global.get $g
    i32.wrap_i64
    i32.load offset=16
    i32.le_u
    if
      local.get $spc
      i32.const 8
      i32.sub
      local.tee $spc
      global.set $sp
      local.get $spc
      i32.const 4660
      i32.store
      i32.const 0
      call $xwasm
    end
    i32.const 9)
)
