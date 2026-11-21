(module
  (import "env" "__stack_pointer" (global $sp (mut i32)))
  (import "env" "memory" (memory 1))
  (global $g (mut i32) (i32.const 0))
  (func $morestack (result i32) i32.const 0)
  (func (export "a3_canonical_hidden") (param i32) (result i32)
    (local i32)
    global.get $sp
    i32.const 16
    i32.sub
    local.set 1
    global.get $sp
    i32.const 16
    i32.add
    i32.load
    if
      nop
    end
    i32.const 7)
  (func (export "a3_else_kept") (param i32) (result i32)
    local.get 0
    if (result i32)
      call $morestack
    else
      i32.const 9
    end)
  (func (export "a3_no_morestack_kept") (param i32) (result i32)
    (local i32)
    global.get $sp
    i32.const 16
    i32.add
    i32.load
    if
      i32.const 5
      global.set $g
    end
    i32.const 3)
)
