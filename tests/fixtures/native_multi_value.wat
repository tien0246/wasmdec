(module
  (type $pair (func (result i32 i32)))
  (type $six_words (func (result i32 i64 i64 i32)))
  (func $make_pair (type $pair)
    i32.const 17
    i32.const 34)
  (func $forward_pair (type $pair)
    call $make_pair)
  (func $make_six_words (type $six_words)
    i32.const 17
    i64.const 34
    i64.const 51
    i32.const 68)
  (func $forward_six_words (type $six_words)
    call $make_six_words)
  (export "make_pair" (func $make_pair))
  (export "forward_pair" (func $forward_pair))
  (export "make_six_words" (func $make_six_words))
  (export "forward_six_words" (func $forward_six_words)))
