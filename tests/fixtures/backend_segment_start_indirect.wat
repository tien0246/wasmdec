(module
  (type $void (func))
  (table 1 funcref)
  (memory 1)
  (elem (i32.const 0) $dropper)
  (data $passive "p")
  (func $start
    call $dispatch)
  (start $start)
  (func $dispatch
    i32.const 0
    call_indirect (type $void))
  (func $dropper
    data.drop $passive)
  (func (export "read")
    i32.const 0 i32.const 0 i32.const 1 memory.init $passive)
)
