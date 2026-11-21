(module
  (type $tuple (func (result i32 i64 i32 i32)))
  (func $make (type $tuple)
    i32.const 287454020
    i64.const 1161981756646125696
    i32.const -559038737
    i32.const 1432778632)
  (func $forward (type $tuple)
    call $make)
  (export "make" (func $make))
  (export "forward" (func $forward)))
