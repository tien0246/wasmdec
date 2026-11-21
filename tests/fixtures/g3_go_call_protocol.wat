(module
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

  ;; go-style caller: prologue SP copy + block ladder + one full G3
  ;; call-protocol sequence, byte-identical to the go1.26.5 dumps.
  (func (export "go_caller") (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    block $unwind
      loop $entry
        local.get 0
        br_table 0 1
        ;; --- body block 0: SP-=8(res); args: SP-=16 first, 2 stores
        local.get 1
        i32.const 24
        i32.sub
        local.tee 1
        global.set $sp
        local.get 1
        i64.const 0x2A
        i64.store
        local.get 1
        i32.const 77
        i32.store offset=8
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
        ;; resume body inline
        local.get 1
        i32.const 16
        i32.add
        local.set 1
        local.get 1
        i32.load
        return
      end
    end
    i32.const 1)

  (func (export "go_caller_mutated") (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    block $unwind
      loop $entry
        local.get 0
        br_table 0 1
        local.get 1
        i32.const 24
        i32.sub
        local.tee 1
        global.set $sp
        local.get 1
        i64.const 0x2A
        i64.store
        local.get 1
        i32.const 77
        i32.store offset=8
        local.get 1
        i32.const 8
        i32.sub
        local.tee 1
        global.set $sp
        local.get 1
        i64.const 0x10001
        i64.store offset=8 ;; ONE byte off the canonical protocol
        i32.const 0
        call $callee
        global.get $sp
        local.set 1
        br_if $unwind
        local.get 1
        i32.const 16
        i32.add
        local.set 1
        local.get 1
        i32.load
        return
      end
    end
    i32.const 1)
)
