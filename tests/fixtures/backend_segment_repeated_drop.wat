(module
  (memory 1)
  (data $passive "p")
  (func (export "drop")
    data.drop $passive)
  (func (export "read")
    i32.const 0 i32.const 0 i32.const 1 memory.init $passive)
)
