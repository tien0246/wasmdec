(module
  (type $void (func))
  (table 1 funcref)
  (memory 1)
  (elem (i32.const 0) $dropper)
  (data $passive "p")
  (global $index (mut i32) (i32.const 0))
  (func $start
    global.get $index
    call_indirect (type $void))
  (start $start)
  (func $dropper
    data.drop $passive)
  (func (export "read")
    i32.const 0 i32.const 0 i32.const 1 memory.init $passive)
)
