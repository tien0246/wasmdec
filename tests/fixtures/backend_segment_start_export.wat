(module
  (memory 1)
  (data (i32.const 0) "a")
  (data $passive "p")
  (func $start
    data.drop $passive)
  (start $start)
  (func $leaf
    nop)
  (func $helper
    call $leaf)
  (func (export "read")
    call $helper
    i32.const 0 i32.const 0 i32.const 1 memory.init $passive)
)
