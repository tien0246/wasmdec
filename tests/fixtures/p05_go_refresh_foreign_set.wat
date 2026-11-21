(module
  ;; P0-5: a byte-exact go1.26 call protocol whose post-call tail carries a
  ;; FOREIGN local.set (of $keep) and an extra `global.get $sp` whose next
  ;; instruction is NOT a local.set. The checker (corrupt blob 'x' so the
  ;; recorded SP-refresh points at that planted global.get) proves the
  ;; consumption contract:
  ;;   - old order-flag lowering: flag on at the planted refresh EA, the
  ;;     next local.set ANYWHERE ($keep's) is swallowed silently — the
  ;;     assignment disappears from the pseudocode;
  ;;   - P0-5 EA+index lowering: the shape `global.get 0; local.set L` is
  ;;     verified during collection; the planted EA fails it, the whole
  ;;     record set is dropped (S8) and EVERY instruction renders —
  ;;     `v = wasm_global_0 + 5` stays in the output.
  ;;
  ;; Block layout mirrors the real go1.26 shells (see s2_go_sp_stable.wat):
  ;; the entry loop's br_table jumps past `unreachable` so the body is
  ;; reachable, and the protocol br_if can see $unwind.
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

  ;; provenance majority dummies (PC_B signature + SP-copy prologue)
  (func $dummy1 (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    (local.get $pcb))
  (func $dummy2 (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    (i32.const 7))
  (func $callee (param $pcb i32) (result i32)
    (i32.add (local.get $pcb) (i32.const 1)))

  (func (export "p05_foreign_set") (param $pcb i32) (result i32)
    (local $sp i32)
    (local $keep i32)
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
      ;; the recorded protocol: SP-=8 leg, return-slot store, PC_B push,
      ;; call, SP refresh pair, unwind br_if (byte-exact for scan_go_calls)
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
      ;; the tail under test: a planted `global.get $sp` whose consumer is
      ;; an add (not a local.set), then the FOREIGN local.set of $keep.
      ;; $keep is RETURNED — it must stay live so the rendering difference
      ;; is visible (a dead store is eliminated in both builds).
      i32.const 100
      global.get $sp
      i32.const 5
      i32.add
      local.set $keep
      i32.const 7
      drop
      drop
      local.get $keep
      return
    end
    i32.const 1)
  (func (export "p05_tail") (result i32)
    (i32.const 21))
)
