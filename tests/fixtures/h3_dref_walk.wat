;; H3 (dref generation): the constant-address evidence shapes.
;;
;;   $walk:
;;     i32.const 0x100; i32.load offset=4                   -> read 0x104
;;     i32.const 0x200; i32.const 8; i32.add; i64.store     -> write 0x208
;;     local.get 0; i32.load offset=0                       -> NOT proven
;;     i32.const 0x300; drop                                 -> no access
;;     global.get $base; i32.load offset=0                   -> NOT proven
;;     global.get $sp                                        -> global ref 0
(module
  (memory 1)
  (global $sp (mut i32) (i32.const 0))
  (global $base (mut i32) (i32.const 0))
  (func (export "walk") (param $dyn i32) (result i32)
    (local $t i32)
    i32.const 0x100
    i32.load offset=4
    local.set $t
    i32.const 0x200
    i32.const 8
    i32.add
    i64.const 7
    i64.store
    local.get $dyn
    i32.load offset=0
    local.set $t
    i32.const 0x300
    drop
    global.get $base
    i32.load offset=0
    local.set $t
    global.get $sp
    local.set $t
    local.get $t
  )
)
