(module
  ;; G6/M4-D4: 2-slot Go write-barrier reproducer, upgraded to the REAL
  ;; compiler protocol (M4/D4: the loader proves this byte shape — see
  ;; collect_go_wb_evidence in core/toolchains/go/ir.cpp; the old simplified
  ;; shape tested the deleted ctree matcher).
  ;;
  ;; The embedded go1.20+ pclntab names the 5 defined functions:
  ;;   0: runtime.gcWriteBarrier1
  ;;   1: runtime.sigpanic
  ;;   2: main.dummy
  ;;   3: main.wb1_collapsed  (guard + in-body nil-check + spills + call +
  ;;                           wbBuf writes + reloads; the compiler-duplicated
  ;;                           nil-check follows the region ->
  ;;                           `if (dst == 0) sigpanic(); *dst = v; // go: write barrier`)
  ;;   4: main.wb1_near_miss  (same protocol but the in-body nil-check is NOT
  ;;                           duplicated after the region -> fails closed
  ;;                           and the barrier stays visible)
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

  ;; defined 0 — pclntab: runtime.gcWriteBarrier1 (real wrapper shape:
  ;; i64.const 16; call core; return; unreachable; end)
  (func $wb2 (result i64)
    i64.const 8
    call $wbcore
    return
    unreachable)
  ;; defined 1 — pclntab: runtime.sigpanic (terminal; Go wasm callees keep
  ;; the i32 PC_B result so the caller's `br_if` redo check is stack-valid)
  (func $sigpanic (param i32) (result i32)
    unreachable)

  ;; helper not in pclntab order — keep the core LAST so indices stay stable.
  ;; defined 2 — pclntab: main.dummy (provenance witness)
  (func $dummy (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    local.get $pcb)

  ;; defined 3 — pclntab: main.wb1_collapsed
  (func (export "wb1_collapsed") (param $pcb i32) (result i32)
    (local $sp i32)     ;; 1
    (local $buf i64)    ;; 2
    (local $val i64)    ;; 3
    (local $dst i64)    ;; 4
    (local $old i64)    ;; 5
    (local $flag i64)   ;; 6
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
          ;; runtime.writeBarrier.enabled -> $flag (static 4-byte read)
          i32.const 2048
          i64.load32_u offset=0
          local.set $flag
          ;; guard: if (!enabled) -> fast path case 1
          local.get $flag
          i64.eqz
          i32.eqz
          if
            i32.const 1
            local.set 0
            br $wb_done
          end
          ;; in-body nil-check (full sigpanic protocol)
          local.get 4
          i64.eqz
          if
            local.get 1
            i32.const 8
            i32.sub
            local.tee 1
            global.set $sp
            local.get 1
            i64.const 999001
            i64.store offset=0
            i32.const 0
            call $sigpanic
            global.get $sp
            local.set 1
            br_if 0
          end
          ;; spill: [SP+24] = $val
          local.get 1
          local.get 3
          i64.store offset=24
          ;; old-value chain: $old = *(u64*)($dst + 0)
          local.get 4
          i32.wrap_i64
          i64.load offset=0
          local.set $old
          ;; spill: [SP+16] = $old
          local.get 1
          local.get $old
          i64.store offset=16
          ;; the barrier call
          call $wb2
          ;; SP refresh + wbBuf slot pointer
          global.get $sp
          local.set 1
          local.tee $buf
          i32.wrap_i64
          ;; buffer write 0: [buf+0] = [SP+24]
          local.get 1
          i64.load offset=24
          i64.store offset=0
          ;; reloads
          local.get 1
          i64.load offset=24
          local.set 3
        end
        ;; compiler-duplicated nil-check + the pointer store
        local.get 4
        i64.eqz
        if
          local.get 1
          i32.const 8
          i32.sub
          local.tee 1
          global.set $sp
          local.get 1
          i64.const 999002
          i64.store offset=0
          i32.const 0
          call $sigpanic
          global.get $sp
          local.set 1
          br_if 0
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

  ;; defined 4 — pclntab: main.wb1_near_miss (no duplicated post-region guard)
  (func (export "wb1_near_miss") (param $pcb i32) (result i32)
    (local $sp i32)     ;; 1
    (local $buf i64)    ;; 2
    (local $val i64)    ;; 3
    (local $dst i64)    ;; 4
    (local $old i64)    ;; 5
    (local $flag i64)   ;; 6
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
          i32.const 2048
          i64.load32_u offset=0
          local.set $flag
          local.get $flag
          i64.eqz
          i32.eqz
          if
            i32.const 1
            local.set 0
            br $wb_done
          end
          ;; in-body nil-check NOT duplicated after the region -> MUST skip!
          local.get 4
          i64.eqz
          if
            local.get 1
            i32.const 8
            i32.sub
            local.tee 1
            global.set $sp
            local.get 1
            i64.const 999003
            i64.store offset=0
            i32.const 0
            call $sigpanic
            global.get $sp
            local.set 1
            br_if 0
          end
          local.get 1
          local.get 3
          i64.store offset=24
          local.get 4
          i32.wrap_i64
          i64.load offset=0
          local.set $old
          local.get 1
          local.get $old
          i64.store offset=16
          call $wb2
          global.get $sp
          local.set 1
          local.tee $buf
          i32.wrap_i64
          local.get 1
          i64.load offset=24
          i64.store offset=0
          local.get 1
          i64.load offset=24
          local.set 3
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

  ;; defined 5 (hidden from the pclntab name list): the gcWriteBarrier core
  ;; (wbBuf bump-allocation loop prologue — the exact 19-insn shape the
  ;; loader matches; wb2 calls it with i64.const 16).
  (func $wbcore (param $bytes i64) (result i64)
    (local $m i64)
    (local $p i64)
    (local $next i64)
    loop
      global.get $g
      i32.wrap_i64
      i64.load offset=0x30
      local.tee $m
      i32.wrap_i64
      i64.load offset=0xA0
      local.tee $p
      i32.wrap_i64
      i64.load offset=0xB8
      local.tee $next
      local.get 0
      i64.add
      local.tee $next
      local.get $p
      i32.wrap_i64
      i64.load offset=0xC0
      i64.le_u
      if
        local.get $next
        return
      end
      unreachable
    end
    unreachable)

  (data (i32.const 57056)
    "\f1\ff\ff\ff\00\00\01\08\05\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00"
    "\48\00\00\00\00\00\00\00\a8\00\00\00\00\00\00\00\a8\00\00\00\00\00\00\00\a8\00\00\00\00\00\00\00"
    "\a8\00\00\00\00\00\00\00\72\75\6e\74\69\6d\65\2e\67\63\57\72\69\74\65\42\61\72\72\69\65\72\31\00"
    "\72\75\6e\74\69\6d\65\2e\73\69\67\70\61\6e\69\63\00\6d\61\69\6e\2e\64\75\6d\6d\79\00\6d\61\69\6e"
    "\2e\77\62\31\5f\63\6f\6c\6c\61\70\73\65\64\00\6d\61\69\6e\2e\77\62\31\5f\6e\65\61\72\5f\6d\69\73"
    "\73\00\00\00\00\00\00\00\00\10\00\00\30\00\00\00\01\10\00\00\38\00\00\00\02\10\00\00\40\00\00\00"
    "\03\10\00\00\48\00\00\00\04\10\00\00\50\00\00\00\05\10\00\00\00\00\00\00\00\10\00\00\00\00\00\00"
    "\01\10\00\00\18\00\00\00\02\10\00\00\29\00\00\00\03\10\00\00\34\00\00\00\04\10\00\00\47\00\00\00")
)
