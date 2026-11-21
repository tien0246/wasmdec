(module
  (type $tuple (func (result i64 i64 i64 i32)))
  (table 2 funcref)

  (func $target_a (export "target_a") (type $tuple)
    i64.const 0x0102030405060708
    i64.const 0x1112131415161718
    i64.const 0x0123456789abcdef
    i32.const 0x12345678)
  (func $target_b (export "target_b") (type $tuple)
    i64.const 0x2122232425262728
    i64.const 0x3132333435363738
    i64.const 0x4142434445464748
    i32.const 0x55667788)
  (elem (i32.const 0) func $target_a $target_b)

  (func (export "dispatch") (param $index i32) (result i64 i64 i64 i32)
    local.get $index
    call_indirect (type $tuple)))
