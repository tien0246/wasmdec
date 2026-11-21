(module
  (memory (export "memory") i64 1)

  (func (export "copy_v128_zero_offset") (param $dst i64) (param $src i64)
    local.get $dst
    local.get $src
    v128.load
    v128.store)

  (func (export "copy_v128_offset") (param $dst i64) (param $src i64)
    local.get $dst
    local.get $src
    v128.load offset=16 align=16
    v128.store offset=32 align=16)

  (func (export "copy_v128_high_offset") (param $dst i64) (param $src i64)
    local.get $dst
    local.get $src
    v128.load offset=4294967296 align=16
    v128.store offset=4294967312 align=16)

  (func (export "copy_v128_max_offset") (param $dst i64) (param $src i64)
    local.get $dst
    local.get $src
    v128.load offset=18446744073709551615 align=16
    v128.store offset=18446744073709551615 align=16)

  (func (export "copy_v128_load8_splat") (param $dst i64) (param $src i64)
    local.get $dst
    local.get $src
    v128.load8_splat offset=1 align=1
    v128.store offset=64 align=16)

  (func (export "copy_v128_load16_splat") (param $dst i64) (param $src i64)
    local.get $dst
    local.get $src
    v128.load16_splat offset=2 align=2
    v128.store offset=80 align=16)

  (func (export "copy_v128_load32_splat") (param $dst i64) (param $src i64)
    local.get $dst
    local.get $src
    v128.load32_splat offset=4 align=4
    v128.store offset=96 align=16)

  (func (export "copy_v128_load64_splat") (param $dst i64) (param $src i64)
    local.get $dst
    local.get $src
    v128.load64_splat offset=8 align=8
    v128.store offset=112 align=16)

  (func (export "copy_v128_load32_zero") (param $dst i64) (param $src i64)
    local.get $dst
    local.get $src
    v128.load32_zero offset=12 align=4
    v128.store offset=128 align=16)

  (func (export "copy_v128_load64_zero") (param $dst i64) (param $src i64)
    local.get $dst
    local.get $src
    v128.load64_zero offset=16 align=8
    v128.store offset=144 align=16))
