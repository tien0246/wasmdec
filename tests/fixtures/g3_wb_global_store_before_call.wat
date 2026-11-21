(module
  ;; G3/G4 write-barrier end boundary reproducer — Sub-shape A (global pointer
  ;; store after write-barrier block `end` immediately before a Go call:
  ;; reproduces runtime.initMetrics / wasm_func_361, reflect.addReflectOff /
  ;; wasm_func_1301, wasm_func_2838, wasm_func_3117).
  ;;
  ;; Inside $wb_done, the slow path reloads local 3 from [SP + 24] via
  ;; `local.get 1; i64.load offset=24; local.set 3` right before `end $wb_done`.
  ;; After `end $wb_done`, Go stores local 3 to a global pointer address:
  ;;   i64.const 1024; i32.wrap_i64; local.get 3; i64.store offset=0
  ;; and then sets up outgoing argument 0 at [SP + 0] for `call $callee`.
  ;; The backward argument-store walk in `build_go_function_irs` must stop at
  ;; `end $wb_done` and NOT claim the inner `local.get 1` of the write-barrier
  ;; block as the SP base of the global store (which would strand the address
  ;; `i32.wrap_i64(1024)` on the operand stack with surplus=1 on the fast path
  ;; `br $wb_done`).
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

  (func $wb2 (result i64)
    i64.const 512)
  (func $dummy1 (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    local.get $pcb)
  (func $callee (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    local.get 1
    i64.load offset=8
    i32.wrap_i64)

  (func (export "wb_global_store_before_call") (param $pcb i32) (result i32)
    (local $sp i32)
    (local $wb_buf i64)
    (local $val i64)
    global.get $sp
    local.set 1
    block $unwind
      loop $entry
        block $after_call
          block $wb_done
            block $b0
              local.get 0
              br_table $b0 $wb_done $after_call $unwind
            end
            ;; Fast-path write-barrier check: if disabled, const-JMP to $wb_done
            i64.const 2048
            i32.wrap_i64
            i32.load offset=0
            i32.eqz
            if
              i32.const 1
              local.set 0
              br $wb_done
            end
            ;; Slow-path write barrier: spill $val at [SP + 24], call $wb2, reload $val
            local.get 1
            local.get 3
            i64.store offset=24
            call $wb2
            global.get $sp
            local.set 1
            local.tee 2
            i32.wrap_i64
            local.get 1
            i64.load offset=24
            i64.store offset=0
            local.get 1
            i64.load offset=24
            local.set 3
          end
          ;; Post-barrier global pointer assignment: *(i64*)1024 = $val
          i64.const 1024
          i32.wrap_i64
          local.get 3
          i64.store offset=0
          ;; Outgoing argument 0 at [SP + 0] for Go call to $callee
          local.get 1
          i64.const 77
          i64.store offset=0
          local.get 1
          i32.const 8
          i32.sub
          local.tee 1
          global.set $sp
          local.get 1
          i64.const 0x10002
          i64.store offset=0
          i32.const 0
          call $callee
          global.get $sp
          local.set 1
          br_if $unwind
        end
        i32.const 0
        return
      end
    end
    i32.const 1)
)
