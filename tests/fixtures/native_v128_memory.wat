(module
  (memory 1)

  ;; Exercises both 16-byte memory operations and nonzero memarg offsets.
  (func (export "copy_v128_offset") (param $dst i32) (param $src i32)
    local.get $dst
    local.get $src
    v128.load offset=16 align=16
    v128.store offset=32 align=16)

  (func (export "load_v128_drop") (param $src i32)
    local.get $src
    v128.load offset=48 align=4
    drop)
)
