(module
  (func (export "dispatch") (param i32) (result i32)
    (local $r i32)
    (block $out
      (block $x
        (block $y
          local.get 0
          (br_table $x $y $x $x)
        )
        i32.const 20 local.set $r br $out
      )
      i32.const 10 local.set $r
    )
    local.get $r))
