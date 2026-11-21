(module
  ;; S11/K1: a zero-argument Go call. The blob-'x' record exists with an
  ;; empty argument list; the call must still travel the Go ABI road and
  ;; render as callee() — the old !empty() guard dropped it back onto the
  ;; raw wasm signature, showing a phantom PC_B argument. Block layout
  ;; mirrors the go1.26 shells (br_table case 1 -> end of the resume
  ;; block) so the body is reachable.
  (import "gojs" "runtime.wasmExit" (func $wasmExit (param i32)))
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

  (func $dummy1 (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    local.get $pcb)
  (func $dummy2 (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    i32.const 7)
  (func $callee (param $pcb i32) (result i32)
    (i32.add (local.get $pcb) (i32.const 1)))

  (func (export "s11_zero_arg") (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    block $unwind
      block $resume
        loop $entry
          local.get 0
          br_table 0 2 1
        end
        unreachable
      end
      ;; resume body — zero-argument call, full protocol
      local.get 1
      i32.const 8
      i32.sub
      local.tee 1
      global.set $sp
      local.get 1
      i64.const 0x10004
      i64.store
      i32.const 0
      call $callee
      global.get $sp
      local.set 1
      br_if $unwind
      i32.const 9
      return
    end
    i32.const 1)
)
