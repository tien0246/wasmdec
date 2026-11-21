(module
  ;; P0-6: two IDENTICAL Go stack-guard functions — one calls a callee the
  ;; pclntab names `runtime.morestack`, the other a callee it names
  ;; `runtime.mini`. Only the named-morestack guard may be hidden; the
  ;; identical-but-unnamed shape must stay (a wasm_func_N shell name
  ;; proves nothing on a name-stripped module like main.wasm).
  ;;
  ;; The pclntab below (data segment at 0xC160, go1.20+ layout: magic
  ;; f1ffffff, ptr=8, 5 functab entries in defined-function order) names
  ;; every defined function exactly as gen_fixtures' pclntab-names
  ;; generator does — parse_pclntab recovers them, so the loader names
  ;; func 0 `runtime.morestack` (dotted Go names are valid IDA
  ;; identifiers). Global layout + gojs import + PC_B/prologue majority
  ;; raise the 'G' v1 provenance the rewriter requires (see
  ;; s9_go_morestack_callee.wat).
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

  ;; defined 0 — pclntab: runtime.morestack. Type[0] of every corpus:
  ;; (i32) -> i32 — the result is the redo br_if condition.
  (func $ms (param i32) (result i32) (i32.const 0))
  ;; defined 1 — pclntab: runtime.mini (identical body, NOT morestack)
  (func $plain (param i32) (result i32) (i32.const 0))
  ;; defined 2 — provenance dummy (PC_B signature + SP-copy prologue)
  (func $dummy (param $pcb i32) (result i32)
    (local $sp i32)
    global.get $sp
    local.set 1
    (local.get $pcb))

  ;; defined 3 — guard calling the NAMED growth helper: MUST be hidden.
  ;; G10: the prologue shape is the real wasmobj.go sequence — the
  ;; resume-PC push is an i64.store and the redo br_if closes the
  ;; then-block (the old P0-6 ctree matcher accepted any constant store
  ;; through SP; the IR proof wants the exact compiler bytes).
  (func (export "p06_named_hidden") (param $pc i32) (result i32)
    (local $spc i32)
    global.get $sp
    local.set $spc
    local.get $spc
    global.get $g
    i32.wrap_i64
    i32.load offset=16
    i32.le_u
    if
      local.get $spc
      i32.const 8
      i32.sub
      local.tee $spc
      global.set $sp
      local.get $spc
      i64.const 4660
      i64.store
      i32.const 0
      call $ms
      global.get $sp
      local.set $spc
      br_if 0
    end
    (i32.const 42))

  ;; defined 4 — IDENTICAL shape calling the callee named runtime.mini:
  ;; the guard MUST stay (the proven-morestack callee is the only
  ;; discriminator — an identical body calling anything else never hides)
  (func (export "p06_unnamed_kept") (param $pc i32) (result i32)
    (local $spc i32)
    global.get $sp
    local.set $spc
    local.get $spc
    global.get $g
    i32.wrap_i64
    i32.load offset=16
    i32.le_u
    if
      local.get $spc
      i32.const 8
      i32.sub
      local.tee $spc
      global.set $sp
      local.get $spc
      i64.const 4660
      i64.store
      i32.const 0
      call $plain
      global.get $sp
      local.set $spc
      br_if 0
    end
    (i32.const 7))

  ;; pclntab: header (0x48) | funcnametab | functab | funcs — names the
  ;; five defined functions in declaration order:
  ;;   0 runtime.morestack, 1 runtime.mini, 2 main.dummy,
  ;;   3 main.a, 4 main.b
  (data (i32.const 57056)
    "\f1\ff\ff\ff\00\00\01\08\05\00\00\00\00\00\00\00\01\00\00\00\00\00\00\00\00\00\00\00\00\00\00\00"
    "\48\00\00\00\00\00\00\00\80\00\00\00\00\00\00\00\80\00\00\00\00\00\00\00\80\00\00\00\00\00\00\00"
    "\80\00\00\00\00\00\00\00\72\75\6e\74\69\6d\65\2e\6d\6f\72\65\73\74\61\63\6b\00\72\75\6e\74\69\6d"
    "\65\2e\6d\69\6e\69\00\6d\61\69\6e\2e\64\75\6d\6d\79\00\6d\61\69\6e\2e\61\00\6d\61\69\6e\2e\62\00"
    "\00\10\00\00\30\00\00\00\01\10\00\00\38\00\00\00\02\10\00\00\40\00\00\00\03\10\00\00\48\00\00\00"
    "\04\10\00\00\50\00\00\00\05\10\00\00\00\00\00\00\00\10\00\00\00\00\00\00\01\10\00\00\12\00\00\00"
    "\02\10\00\00\1f\00\00\00\03\10\00\00\2a\00\00\00\04\10\00\00\31\00\00\00")

)
