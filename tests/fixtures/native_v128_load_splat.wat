(module
  (memory (export "memory") 1)

  (func (export "copy_load8_splat") (param $dst i32) (param $src i32)
    local.get $dst
    local.get $src
    v128.load8_splat offset=1 align=1
    v128.store)

  (func (export "copy_load16_splat") (param $dst i32) (param $src i32)
    local.get $dst
    local.get $src
    v128.load16_splat offset=2 align=2
    v128.store)

  (func (export "copy_load32_splat") (param $dst i32) (param $src i32)
    local.get $dst
    local.get $src
    v128.load32_splat offset=4 align=4
    v128.store)

  (func (export "copy_load64_splat") (param $dst i32) (param $src i32)
    local.get $dst
    local.get $src
    v128.load64_splat offset=8 align=8
    v128.store))
