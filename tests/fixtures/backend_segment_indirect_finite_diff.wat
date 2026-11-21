(module
  (type $void (func))
  (table 2 funcref)
  (memory 1)
  (elem (i32.const 0) $drop_a $keep)
  (data $passive "p")
  (global $condition (mut i32) (i32.const 0))
  (func $start
    call $dispatch)
  (start $start)
  (func $dispatch
    global.get $condition
    if (result i32)
      i32.const 0
    else
      i32.const 1
    end
    call_indirect (type $void))
  (func $drop_a
    data.drop $passive)
  (func $keep
    nop)
  (func (export "read")
    i32.const 0 i32.const 0 i32.const 1 memory.init $passive)
)
