(module
  (type $i32ref (func (result i32 externref)))
  (func (export "branch_null") (param $ref externref) (result i32)
    (block $null (result i32)
      i32.const 22
      i32.const 11
      local.get $ref
      br_on_null $null
      drop
      drop
      drop
      i32.const 33))
  (func (export "branch_non_null") (param $ref externref) (result i32)
    (block $done (type $i32ref)
      i32.const 99
      i32.const 4
      local.get $ref
      br_on_non_null $done
      drop
      drop
      i32.const 5
      ref.null extern)
    ref.is_null
    i32.add)
  (func (export "branch_null_return") (param $ref externref) (result i32)
    i32.const 11
    local.get $ref
    br_on_null 0
    drop
    drop
    i32.const 33)
  (func (export "branch_non_null_return") (param $ref externref) (result externref)
    local.get $ref
    br_on_non_null 0
    ref.null extern)
  (func (export "branch_null_loop") (param $candidate externref) (param $ref externref)
      (result i32) (local $current externref)
    (block $exit (result i32)
      ref.null extern
      (loop $again (param externref)
        local.set $current
        i32.const 99
        local.get $candidate
        local.get $current
        br_on_null $again
        drop
        drop
        drop
        i32.const 42
        br $exit)
      unreachable))
)
