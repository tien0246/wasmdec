(module
  ;; G6 (todo §14 / 0363d22 + structural gate): write-barrier collapse on a
  ;; FULLY STRIPPED Go module (no pclntab, no name section — every defined
  ;; function is a synthetic `wasm_func_N`), reproducing the main.wasm
  ;; `wasm_func_1301` / `wasm_func_1806` shapes:
  ;;   - `runtime.gcWriteBarrier2` is proven by the 2-level BYTECODE proof
  ;;     (`gc_write_barrier_index_t`: wrapper body `i64.const 16; call core;
  ;;     return; unreachable` + the core `wbBuf` bump-allocation loop prologue).
  ;;   - the duplicated nil-check guard `if (dst == 0) CALLEE();` is proven
  ;;     STRUCTURALLY for the terminal helper (valid Go IR, zero 0x0F return,
  ;;     forward-only const-JMP — `is_go_terminal_noreturn_function`, the same
  ;;     proof that built the module-level noreturn callee set at 93afba1).
  ;;
  ;; Defined bodies (module function index = 1 + body index, one import):
  ;;   0: $gcwb_core        (fn 1)  gcWriteBarrier core, wbBuf loop prologue
  ;;   1: $wb2              (fn 2)  gcWriteBarrier2 wrapper (bytecode-proven)
  ;;   2: $sigpanic_like    (fn 3)  terminal helper — zero 0x0F, forward JMP
  ;;   3: $loop_helper      (fn 4)  near-miss helper — zero 0x0F, BACKWARD JMP
  ;;   4: $wb_struct_collapsed (fn 5) 2-slot wb + duplicated guard on
  ;;                                $sigpanic_like -> collapses to the
  ;;                                post-if guard + `*dst = val; // go: write barrier`
  ;;   5: $wb_struct_loop_near_miss (fn 6) identical shape but the guard calls
  ;;                                $loop_helper (fails the terminal proof)
  ;;                                -> fails closed, wb block stays visible.
  (import "gojs" "runtime.wasmExit" (func $wasmExit (param i32)))
  (global $sp (mut i32) (i32.const 0))
  (global $ctxt (mut i64) (i64.const 0))
  (global $g (mut i64) (i64.const 0))
  (global $ret0 (mut i64) (i64.const 0))
  (global $ret1 (mut i64) (i64.const 0))
  (global $ret2 (mut i64) (i64.const 0))
  (global $ret3 (mut i64) (i64.const 0))
  (global $pause (mut i32) (i32.const 0))
  (memory 1)

  ;; Defined body 0 (fn 1): gcWriteBarrier core — the `wbBuf` bump-allocation
  ;; loop prologue matched instruction-exact by `match_gc_write_barrier_core_body`
  ;; (loop / global.get 2 / g+0x30 -> m / m+0xA0 -> p / p+wb_next -> next /
  ;; next+bytes / p+wb_next+8 (wb_end) / i64.le_u / if ...). The `if` body is
  ;; padded with nops so the core reaches the 60..256 byte window the index
  ;; verification requires on stripped modules.
  (func $gcwb_core (param $bytes i64) (result i64)
    (local $m i64)
    (local $p i64)
    (local $next i64)
    loop $scan
      global.get $g
      i32.wrap_i64
      i64.load offset=48
      local.tee $m
      i32.wrap_i64
      i64.load offset=160
      local.tee $p
      i32.wrap_i64
      i64.load offset=8
      local.tee $next
      local.get $bytes
      i64.add
      local.tee $next
      local.get $p
      i32.wrap_i64
      i64.load offset=16
      i64.le_u
      if
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
        nop
      end
    end
    i64.const 0)

  ;; Defined body 1 (fn 2): gcWriteBarrier2 wrapper — `i64.const 16; call
  ;; $gcwb_core; return; unreachable` (16 = 2 slots * 8 bytes, the core's
  ;; `bytes` argument), the exact 5-instruction body matched by
  ;; `match_gc_write_barrier_wrapper_body`.
  (func $wb2 (result i64)
    i64.const 16
    call $gcwb_core
    return
    unreachable)

  ;; Defined body 2 (fn 3): terminal sigpanic-like helper (zero 0x0F return,
  ;; forward-only const-JMP) — satisfies `is_go_terminal_noreturn_function`.
  (func $sigpanic_like (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    block $unwind
      loop $entry
        block $b1
          block $b0
            local.get 0
            br_table $b0 $b1 $unwind
          end
          local.get 1
          i32.const 16
          i32.sub
          local.tee 1
          global.set $sp
          local.get 1
          i64.load offset=24
          local.get 1
          i64.load offset=32
          i64.add
          drop
          ;; Forward const-JMP to $b1 (target_offset > br_offset)
          i32.const 1
          local.set 0
          br $b1
        end
        i32.const 2
        call $wasmExit
        unreachable
      end
    end
    i32.const 1)

  ;; Defined body 3 (fn 4): near-miss helper (zero 0x0F return, WITH a
  ;; backward const-JMP edge) — must NOT satisfy the terminal-noreturn proof.
  (func $loop_helper (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    block $unwind
      loop $entry
        block $b1
          block $b0
            local.get 0
            br_table $b0 $b1 $unwind
          end
          local.get 1
          i32.const 16
          i32.sub
          local.tee 1
          global.set $sp
          local.get 1
          i64.load offset=24
          drop
        end
        ;; Backward const-JMP: case 1 targets an earlier offset.
        i32.const 1
        local.set 0
        br $entry
      end
    end
    i32.const 1)

  ;; Defined body 4 (fn 5): POSITIVE — 2-slot write barrier with the
  ;; duplicated nil-check guard calling the structurally-proven terminal
  ;; helper. Collapses to:
  ;;   if (dst == 0) wasm_func_3();
  ;;   *(u64*)dst = val; // go: write barrier
  (func (export "wb_struct_collapsed") (param $pcb i32) (result i32)
    (local $sp i32)
    (local $wb_buf i64)
    (local $val i64)
    (local $dst i64)
    global.get $sp
    local.set 1
    block $unwind
      loop $entry
        block $wb_done
          block $b0
            local.get 0
            br_table $b0 $wb_done $unwind
          end
          local.get 1
          i32.const 48
          i32.sub
          local.tee 1
          global.set $sp
          local.get 1
          i64.load offset=8
          local.set 3
          local.get 1
          i64.load offset=32
          local.set 4
          ;; Fast-path write-barrier check: writeBarrier.enabled == 0 -> $wb_done
          i64.const 2048
          i32.wrap_i64
          i32.load offset=0
          i32.eqz
          if
            i32.const 1
            local.set 0
            br $wb_done
          end
          ;; Slow-path 2-slot write barrier with in-body nil-check guard
          ;; calling the STRIPPED terminal helper (fn 3, no pclntab name):
          ;;   [SP + 24] = $val; [SP + 32] = $dst
          ;;   if ($dst == 0) sigpanic_like(0)
          ;;   [SP + 16] = *(u64*)$dst
          ;;   $wb_buf = wasm_func_2(16)   (bytecode-proven gcWriteBarrier2)
          ;;   $sp = SP; buf[0] = [SP+24]; buf[1] = [$sp+16]
          ;;   $val = [SP+24]; $dst = [SP+32]
          local.get 1
          local.get 3
          i64.store offset=24
          local.get 1
          local.get 4
          i64.store offset=32
          local.get 4
          i64.eqz
          if
            i32.const 0
            call $sigpanic_like
            drop
          end
          local.get 1
          local.get 4
          i32.wrap_i64
          i64.load offset=0
          i64.store offset=16
          call $wb2
          global.get $sp
          local.set 1
          local.tee 2
          i32.wrap_i64
          global.get $sp
          i64.load offset=24
          i64.store offset=0
          local.get 2
          i32.wrap_i64
          local.get 1
          i64.load offset=16
          i64.store offset=8
          local.get 1
          i64.load offset=24
          local.set 3
          local.get 1
          i64.load offset=32
          local.set 4
        end
        ;; Post-barrier identical nil-check guard + pointer assignment:
        ;;   if ($dst == 0) sigpanic_like(0); *(u64*)$dst = $val
        local.get 4
        i64.eqz
        if
          i32.const 0
          call $sigpanic_like
          drop
        end
        local.get 4
        i32.wrap_i64
        local.get 3
        i64.store offset=0
        local.get 1
        i32.const 56
        i32.add
        local.tee 1
        global.set $sp
        i32.const 0
        return
      end
    end
    i32.const 1)

  ;; Defined body 5 (fn 6): NEAR-MISS — identical 2-slot write barrier, but
  ;; the duplicated guard calls $loop_helper (zero 0x0F but WITH a backward
  ;; const-JMP edge -> fails the terminal-noreturn proof). The write-barrier
  ;; block must FAIL CLOSED and stay visible with its buffer writes.
  (func (export "wb_struct_loop_near_miss") (param $pcb i32) (result i32)
    (local $sp i32)
    (local $wb_buf i64)
    (local $val i64)
    (local $dst i64)
    global.get $sp
    local.set 1
    block $unwind
      loop $entry
        block $wb_done
          block $b0
            local.get 0
            br_table $b0 $wb_done $unwind
          end
          local.get 1
          i32.const 48
          i32.sub
          local.tee 1
          global.set $sp
          local.get 1
          i64.load offset=8
          local.set 3
          local.get 1
          i64.load offset=32
          local.set 4
          i64.const 2048
          i32.wrap_i64
          i32.load offset=0
          i32.eqz
          if
            i32.const 1
            local.set 0
            br $wb_done
          end
          local.get 1
          local.get 3
          i64.store offset=24
          local.get 1
          local.get 4
          i64.store offset=32
          local.get 4
          i64.eqz
          if
            i32.const 0
            call $loop_helper
            drop
          end
          local.get 1
          local.get 4
          i32.wrap_i64
          i64.load offset=0
          i64.store offset=16
          call $wb2
          global.get $sp
          local.set 1
          local.tee 2
          i32.wrap_i64
          global.get $sp
          i64.load offset=24
          i64.store offset=0
          local.get 2
          i32.wrap_i64
          local.get 1
          i64.load offset=16
          i64.store offset=8
          local.get 1
          i64.load offset=24
          local.set 3
          local.get 1
          i64.load offset=32
          local.set 4
        end
        local.get 4
        i64.eqz
        if
          i32.const 0
          call $loop_helper
          drop
        end
        local.get 4
        i32.wrap_i64
        local.get 3
        i64.store offset=0
        local.get 1
        i32.const 56
        i32.add
        local.tee 1
        global.set $sp
        i32.const 0
        return
      end
    end
    i32.const 1)
)
