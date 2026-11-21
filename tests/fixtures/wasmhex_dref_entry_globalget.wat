(module
  ;; T0 rev40 regression: a function whose FIRST instruction is a
  ;; global.get of a DEFINED global. The H3 dref slice (rev39) stored the
  ;; proven global reference at the instruction EA under tags 'R'/'r' —
  ;; 'R' is also the startup-helper string slot read at the function
  ;; entry, so this shape made the decompiler parse an 8-byte ea_t as the
  ;; helper name and refuse F5 with MERR_BADARCH (-31) on every such
  ;; function (measured: 43/102 on an Emscripten/musl module, all of
  ;; main/check_password/show_flag/hex_to_bytes included). The records
  ;; now live under '8'/'9' (netnode_tags.hpp dref_target/dref_kind).
  (import "env" "memory" (memory 1 1))
  (global $sp (mut i32) (i32.const 1024))
  (func (export "entry_global_get") (param i32) (result i32)
    global.get $sp
    local.get 0
    i32.add
    global.set $sp
    global.get $sp)
  (func (export "plain") (param i32) (result i32)
    local.get 0
    i32.const 7
    i32.add)
)
