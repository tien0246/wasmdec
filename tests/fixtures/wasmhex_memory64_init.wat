(module
  (memory i64 1)
  (data $payload "initial-data")

  (func (export "init64") (param $dst i64) (param $src i32) (param $len i32)
    local.get $dst
    local.get $src
    local.get $len
    memory.init $payload)
  (func (export "drop_data64")
    data.drop $payload))
