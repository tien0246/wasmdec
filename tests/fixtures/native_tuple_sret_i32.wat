(module
  (type $triple32 (func (result i32 i32 i32)))

  (func $make_triple32 (export "make_triple32") (type $triple32)
    i32.const 0x11223344
    i32.const 0x22334455
    i32.const 0x33445566)
  (func $forward_triple32 (export "forward_triple32") (type $triple32)
    call $make_triple32)
  (func (export "sum_triple32") (result i32)
    call $make_triple32
    i32.add
    i32.add))
