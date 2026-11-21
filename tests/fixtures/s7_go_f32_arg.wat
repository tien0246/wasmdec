(module
  ;; S7/K1: a Go call carrying one f32 argument. The argument store is a
  ;; real f32.store (0x38); the scanner used to match 0x2C (i32.load8_s)
  ;; so f32 arguments never registered and the call rendered argumentless
  ;; with a raw linear-memory store next to it. Block layout mirrors the
  ;; go1.26 shells (br_table case 1 -> end of the resume block) so the
  ;; body is reachable.
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

  (func (export "s7_f32_arg") (param $pcb i32) (result i32)
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
      ;; resume body — one f32 argument at sp+8, then the full protocol
      local.get 1
      f32.const 1.5
      f32.store offset=8
      local.get 1
      i32.const 8
      i32.sub
      local.tee 1
      global.set $sp
      local.get 1
      i64.const 0x10003
      i64.store
      i32.const 0
      call $callee
      global.get $sp
      local.set 1
      br_if $unwind
      i32.const 6
      return
    end
    i32.const 1)
)
