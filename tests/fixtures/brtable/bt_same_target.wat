(module
  (func (export "dispatch") (param i32) (result i32)
    (local $r i32)
    (block $out
      (block $odd
        (block $even
          local.get 0
          (br_table $even $even $even $even $even $even $even $even $odd)
        )
        i32.const 11 local.set $r br $out
      )
      i32.const 7 local.set $r
    )
    local.get $r))
