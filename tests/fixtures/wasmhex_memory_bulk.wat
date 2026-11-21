(module
  (memory 1)

  (func (export "copy32") (param $dst i32) (param $src i32) (param $len i32)
    local.get $dst
    local.get $src
    local.get $len
    memory.copy)
  (func (export "fill32") (param $dst i32) (param $value i32) (param $len i32)
    local.get $dst
    local.get $value
    local.get $len
    memory.fill))
