(module
  (type $void (func))
  (table 1 funcref)
  (memory 1)
  (elem (i32.const 0) $dropper)
  (data $passive "p")
  (func $start
    i32.const 0
    call_indirect (type $void))
  (start $start)
  (func $dropper
    data.drop $passive)
  (func $mutate_table
    i32.const 0
    ref.func $dropper
    table.set 0)
  (func (export "read")
    i32.const 0 i32.const 0 i32.const 1 memory.init $passive)
)
