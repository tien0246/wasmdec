(module
  (type $pair64 (func (result i64 i64)))
  (type $triple64 (func (param i64) (result i64 i64 i64)))

  (func $make_pair64 (export "make_pair64") (type $pair64)
    i64.const 0x1122334455667788
    i64.const 0x0102030405060708)
  (func $forward_pair64 (export "forward_pair64") (type $pair64)
    call $make_pair64)
  (func (export "sum_pair64") (result i64)
    call $make_pair64
    i64.add)

  (func $make_triple64 (export "make_triple64") (type $triple64) (param $seed i64) (result i64 i64 i64)
    local.get $seed
    i64.const 0x0102030405060708
    i64.const 0x0001020304050607)
  (func $forward_triple64 (export "forward_triple64") (type $triple64) (param $seed i64) (result i64 i64 i64)
    local.get $seed
    call $make_triple64)
  (func (export "sum_triple64") (param $seed i64) (result i64)
    local.get $seed
    call $make_triple64
    i64.add
    i64.add))
