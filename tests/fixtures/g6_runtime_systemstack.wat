(module
  ;; G6-runtime: `runtime.systemstack` fast-path + g0 switch reproducer.
  ;;
  ;; Reproduces the exact fast-path (`g == m.g0`) indirect call in
  ;; `runtime.systemstack` (`cmd/internal/obj/wasm`) where the target closure
  ;; is invoked directly via `call_indirect` followed by `global.get SP;
  ;; local.set 1; return` (no `SP -= 8; i64.store ret_pc; ...; br_if unwind`),
  ;; paired with the slow-path g0 stack switch that uses the standard PC_F
  ;; call protocol.
  (type $pcb_fn (func (param i32) (result i32)))
  (import "gojs" "runtime.wasmExit" (func $wasmExit (param i32)))
  (global $sp (mut i32) (i32.const 0))
  (global $ctxt (mut i64) (i64.const 0))
  (global $g (mut i64) (i64.const 0))
  (global $ret0 (mut i64) (i64.const 0))
  (global $ret1 (mut i64) (i64.const 0))
  (global $ret2 (mut i64) (i64.const 0))
  (global $ret3 (mut i64) (i64.const 0))
  (global $pause (mut i32) (i32.const 0))
  (table 2 funcref)
  (elem (i32.const 1) $worker1)
  (memory 1)

  (func $worker1 (type $pcb_fn) (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    local.get $pcb)
  (func $worker2 (type $pcb_fn) (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    i32.const 0)

  (func (export "runtime_systemstack") (type $pcb_fn) (param $pcb i32) (result i32)
    (local $sp i32)
    (local $fn i64)
    (local $m i64)
    (local $g0 i64)
    (local $g0_sp i64)
    global.get $sp
    local.set 1
    block $unwind
      block $b1
        block $b0
          local.get 0
          br_table $b0 $b0 $b0 $b1
        end
        local.get 1
        i64.load offset=8 align=8
        local.set 2
        global.get $g
        i32.wrap_i64
        i64.load offset=48 align=8
        local.tee 3
        i32.wrap_i64
        i64.load offset=0 align=8
        local.set 4
        global.get $g
        local.get 4
        i64.eq
        if
          local.get 2
          global.set $ctxt
          global.get $ctxt
          i32.wrap_i64
          i64.load offset=0 align=8
          i64.const 16
          i64.shr_u
          i32.wrap_i64
          local.set 0
          i32.const 0
          local.get 0
          call_indirect (type $pcb_fn)
          global.get $sp
          local.set 1
          return
        end
        local.get 4
        global.set $g
        local.get 2
        global.set $ctxt
        global.get $ctxt
        i32.wrap_i64
        i64.load offset=0 align=8
        local.get 1
        i32.const 8
        i32.sub
        local.tee 1
        global.set $sp
        local.get 1
        i64.const 0x10003
        i64.store offset=0 align=8
        i64.const 16
        i64.shr_u
        i32.wrap_i64
        local.set 0
        i32.const 0
        local.get 0
        call_indirect (type $pcb_fn)
        global.get $sp
        local.set 1
        br_if $unwind
      end
      local.get 1
      i32.const 8
      i32.add
      local.tee 1
      global.set $sp
      i32.const 0
      return
      unreachable
    end
    i32.const 1)
)
