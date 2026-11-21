(module
  ;; G3/G4 write-barrier end boundary reproducer — Sub-shape B (local pointer
  ;; heap store after write-barrier block `end` immediately before a Go call:
  ;; reproduces sync.(*poolDequeue).popTail / wasm_func_1547,
  ;; sync.OnceFunc.func1.1.1 / wasm_func_2425, and wasm_func_2422).
  ;;
  ;; Inside $wb_done, the slow path reloads pointer local 3 from [SP + 16] via
  ;; `local.get 1; i64.load offset=16; local.set 3` right before `end $wb_done`.
  ;; After `end $wb_done`, Go clears the heap slot `*ptr = 0`:
  ;;   local.get 3; i32.wrap_i64; i64.const 0; i64.store offset=0
  ;; and then invokes a 0-argument Go call (`call $callee`).
  ;; The backward argument-store walk in `build_go_function_irs` must stop at
  ;; `end $wb_done` so `*ptr = 0` is not misclassified as an outgoing call
  ;; argument (which would strand `i32.wrap_i64(ptr)` on the operand stack with
  ;; surplus=1 at the next block `end`).
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

  (func $wb1 (result i64)
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
    i32.const 0)

  (func (export "wb_local_ptr_store_before_call") (param $pcb i32) (result i32)
    (local $sp i32)
    (local $wb_buf i64)
    (local $ptr i64)
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
            ;; Load closure/heap pointer into local 3 ($ptr)
            global.get $ctxt
            i32.wrap_i64
            i64.load offset=8
            local.set 3
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
            ;; Slow-path write barrier: spill $ptr at [SP + 16], call $wb1, reload $ptr
            local.get 1
            local.get 3
            i64.store offset=16
            call $wb1
            global.get $sp
            local.set 1
            local.tee 2
            i32.wrap_i64
            local.get 3
            i32.wrap_i64
            i64.load offset=0
            i64.store offset=0
            local.get 1
            i64.load offset=16
            local.set 3
          end
          ;; Post-barrier local-pointer store: *(i64*)$ptr = 0
          local.get 3
          i32.wrap_i64
          i64.const 0
          i64.store offset=0
          ;; Zero-argument Go call to $callee (runtime.gorecover shape)
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
