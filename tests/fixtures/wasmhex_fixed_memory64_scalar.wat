(module
  (memory i64 1)
  (func (export "read_i64") (param $address i64) (result i64)
    local.get $address
    i64.load)
  (func (export "write_i64") (param $address i64) (param $value i64)
    local.get $address
    local.get $value
    i64.store))
