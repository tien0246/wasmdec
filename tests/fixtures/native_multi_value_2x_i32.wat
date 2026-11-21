(module
  (type $pair (func (result i32 i32)))
  (func $make_pair (type $pair)
    i32.const 17
    i32.const 34)
  (func $forward_pair (type $pair)
    call $make_pair)
  (export "make_pair" (func $make_pair))
  (export "forward_pair" (func $forward_pair)))
