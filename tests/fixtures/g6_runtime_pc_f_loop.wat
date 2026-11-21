(module
  ;; G6-runtime: `wasm_pc_f_loop` outer PC_F trampoline loop reproducer.
  ;;
  ;; The 41-byte `wasm_pc_f_loop` body matches `cmd/internal/obj/wasm`'s
  ;; outer dispatch loop instruction-for-instruction:
  ;;   if (PAUSE == 0) {
  ;;     do {
  ;;       call_indirect(table0[*(u32*)(SP - 6)])(*(u16*)(SP - 8));
  ;;     } while (PAUSE == 0);
  ;;   }
  ;;   PAUSE = 0;
  ;;   return;
  (type $pcb_fn (func (param i32) (result i32)))
  (type $void_fn (func))
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
  (func $worker3 (type $pcb_fn) (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    i32.const 1)

  (func (export "wasm_pc_f_loop") (type $void_fn)
    global.get $pause
    i32.eqz
    if
      loop
        global.get $sp
        i32.const 8
        i32.sub
        i32.load16_u offset=0 align=2
        global.get $sp
        i32.const 8
        i32.sub
        i32.load offset=2 align=4
        call_indirect (type $pcb_fn)
        drop
        global.get $pause
        i32.eqz
        br_if 0
      end
    end
    i32.const 0
    global.set $pause
    return
    unreachable)
)
