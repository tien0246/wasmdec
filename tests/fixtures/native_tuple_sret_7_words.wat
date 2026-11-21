(module
  (type $quad (func (result i64 i64 i64 i32)))

  (func $make_quad (export "make_quad") (type $quad)
    i64.const 0x0102030405060708
    i64.const 0x1112131415161718
    i64.const 0x0123456789abcdef
    i32.const 0x12345678)
  (func $forward_quad (export "forward_quad") (type $quad)
    call $make_quad)
  (func (export "sum_quad") (result i64)
    call $make_quad
    i64.extend_i32_u
    i64.add
    i64.add
    i64.add))
