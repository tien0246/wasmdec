(module
  (memory i64 1 2)
  (func (export "grow") (result i64)
    i64.const 1
    memory.grow)
  (func (export "size") (result i64)
    memory.size)
  (func (export "touch") (param $address i64) (param $value i32) (result i32)
    local.get $address
    local.get $value
    i32.store offset=4
    local.get $address
    i32.load offset=4))
