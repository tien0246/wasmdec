(module
  (func (export "dispatch") (param i32) (result i32)
    (local $r i32)
    (block $outer
      (block $odd
        (block $even
          local.get 0
          (br_table $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $odd $even $outer $outer)
        )
        i32.const 100 local.set $r br $outer
      )
      i32.const 101 local.set $r br $outer
    )
    local.get $r))
