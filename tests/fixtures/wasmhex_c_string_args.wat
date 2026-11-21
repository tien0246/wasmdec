;; C string args (P0-1 proof extension): constant arguments at proven
;; pointer-typed positions.
;;
;;   $sink:  param 0 is a load/store address base — direct body proof.
;;   $hex2:  param 0 is spilled to a C-frame cell and reloaded into
;;           sscanf arg 0 (a known-libc pointer position) — transitive
;;           proof through the spill cell. param 1 only feeds the varargs
;;           area (sscanf arg 2 is NOT a table pointer position) and must
;;           stay unproven.
;;   $main:  call sites with constant arguments:
;;             sink(0x900)              -> BSS      -> &unk_900
;;             sscanf(0x404, 0x400, ..) -> data     -> literals ("Good job!", "%2x")
;;             hex2(0x420, 0x940)       -> binary data / unproven position
(module
  (import "env" "sscanf" (func $sscanf (param i32 i32 i32) (result i32)))
  (import "env" "__stack_pointer" (global $sp (mut i32)))
  (memory (export "memory") 1)
  (data (i32.const 0x400) "%2x\00")
  (data (i32.const 0x404) "Good job!\00")
  (data (i32.const 0x420) "\01\02\03\00")

  (func $sink (export "sink") (param $ctx i32)
    local.get $ctx
    i32.const 0
    i32.store offset=8
  )

  (func $hex2 (export "hex2") (param $hex i32) (param $out i32) (result i32)
    (local $fp i32)
    (local $reload i32)
    global.get $sp
    i32.const 16
    i32.sub
    global.set $sp
    global.get $sp
    local.set $fp
    local.get $fp
    local.get $hex
    i32.store offset=12
    local.get $fp
    i32.load offset=12
    local.set $reload
    local.get $reload
    i32.const 0x400
    local.get $fp
    call $sscanf
    local.set $reload
    local.get $fp
    i32.const 16
    i32.add
    global.set $sp
    local.get $reload
  )

  (func $main (export "main")
    i32.const 0x900
    call $sink
    i32.const 0x404
    i32.const 0x400
    i32.const 0x940
    call $sscanf
    drop
    i32.const 0x420
    i32.const 0x940
    call $hex2
    drop
  )
)
