(module
  (memory 1)
  (data $payload "initial-data")

  (func (export "init32") (param $dst i32) (param $src i32) (param $len i32)
    local.get $dst
    local.get $src
    local.get $len
    memory.init $payload)
  (func (export "drop_data")
    data.drop $payload))
