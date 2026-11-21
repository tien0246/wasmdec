(module
  (memory i64 1)

  (func (export "copy64") (param $dst i64) (param $src i64) (param $len i64)
    local.get $dst
    local.get $src
    local.get $len
    memory.copy)
  (func (export "fill64") (param $dst i64) (param $value i32) (param $len i64)
    local.get $dst
    local.get $value
    local.get $len
    memory.fill))
