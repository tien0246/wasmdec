(module
  (type $t (func (param i32 i32 i64 i32)))
  (import "env" "memory" (memory 1 1))
  (func (export "f") (type $t)
    (local i64 i64 i64 i64 i64 i64 i64)
    i64.const 63
    local.set 4
    i64.const 0
    local.set 5
    loop
      local.get 7
      local.get 9
      local.get 4
      i64.const 65
      i64.lt_u
      local.tee 1
      select
      local.get 4
      i64.const 1
      i64.sub
      local.tee 10
      i64.shr_u
      local.set 7
      local.get 4
      i64.const 1
      i64.sub
      local.tee 4
      i64.eqz
      i32.eqz
      br_if 0
    end
    return
  )
)
