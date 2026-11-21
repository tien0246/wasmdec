(module
  ;; C++4/K1: `__memory_base + K` as a char* call argument renders as the
  ;; string literal when K addresses one in the ACTIVE data segments
  ;; (the loader mirrors them and records the DATA base under 'L').
  ;; Runtime convention: __memory_base == the first data offset (1024),
  ;; so pointer == __memory_base + K addresses linear 1024 + K.
  (import "env" "memory" (memory 1))
  (import "env" "__memory_base" (global $mb (mut i32)))
  (import "env" "takes_str" (func $takes_str (param i32) (result i32)))
  (data (i32.const 1024) "\00\00")
  (data (i32.const 4096) "cpp4 works\00")
  (func (export "cpp4_string_literal") (result i32)
    global.get $mb
    i32.const 3072
    i32.add
    call $takes_str)
  ;; same arithmetic but pointing at non-string bytes: must stay
  (func (export "cpp4_binary_kept") (result i32)
    global.get $mb
    i32.const 1024
    i32.add
    call $takes_str)
)
