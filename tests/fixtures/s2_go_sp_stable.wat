(module
  ;; S2/K1: two byte-exact go1.26 call-protocol sequences back to back.
  ;; The whole protocol (SP-=8 leg, return-slot store, PC_B const, call,
  ;; unwind br_if, SP refresh pair) is consumed as a unit, so the cached
  ;; SP after the second call equals the SP before the first — the old
  ;; rendering kept one `SP -= 8` per call and drifted -8 each time.
  ;;
  ;; Block layout mirrors the real go1.26 shells: the entry loop's
  ;; br_table jumps to the end of the resume block (case 1) so the body
  ;; below is REACHABLE — with the body simply parked after the loop it
  ;; is dead code per the wasm CFG and Hex-Rays drops it.
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

  (func (export "s2_two_calls") (param $pcb i32) (result i32)
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
      ;; resume body — inside $unwind so the protocol br_if can see it
      local.get 1
      i32.const 8
      i32.sub
      local.tee 1
      global.set $sp
      local.get 1
      i64.const 0x10001
      i64.store
      i32.const 0
      call $callee
      global.get $sp
      local.set 1
      br_if $unwind
      local.get 1
      i32.const 8
      i32.sub
      local.tee 1
      global.set $sp
      local.get 1
      i64.const 0x10002
      i64.store
      i32.const 0
      call $callee
      global.get $sp
      local.set 1
      br_if $unwind
      i32.const 5
      return
    end
    i32.const 1)
)
