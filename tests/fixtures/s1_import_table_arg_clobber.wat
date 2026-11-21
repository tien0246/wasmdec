(module
  ;; S1/K1: the checked dynamic call_indirect path — the table is IMPORTED
  ;; (env.tab, no module-local elem), so the loader cannot resolve it
  ;; statically and the call goes through checked_dynamic_indirect with a
  ;; wasm_table_lookup_* helper. The first argument must survive the
  ;; lookup: operand registers are keyed by (depth, type), and with the
  ;; lookup issued while the selector is on top, its result lands in the
  ;; selector's slot instead of clobbering args[0].
  (import "env" "f" (func $f (param i32 i32) (result i32)))
  (import "env" "mem" (memory 1))
  (import "env" "tab" (table 4 funcref))
  (type $sig (func (param i32 i32) (result i32)))
  (func (export "s1_first_arg_survives") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    local.get 1
    call_indirect (type $sig))
  (func (export "s1_swapped") (param i32 i32) (result i32)
    local.get 1
    local.get 0
    local.get 0
    call_indirect (type $sig))
  ;; the A1 repro shape: args[0] is a LOADED value, not a bare parameter
  (func (export "s1_loaded_arg") (param i32 i32) (result i32)
    i32.const 1024
    i32.load
    local.get 1
    local.get 1
    call_indirect (type $sig))
  (data (i32.const 1024) "\11\00\00\00")
)
