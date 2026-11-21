(module
  (memory 1 2)
  (func (export "grow") (result i32)
    i32.const 1
    memory.grow)
  (func (export "touch") (param $address i32) (param $value i32) (result i32)
    local.get $address
    local.get $value
    i32.store offset=4
    local.get $address
    i32.load offset=4))
