(module
  (func (export "dispatch") (param i32) (result i32)
    (local $r i32)
    (block $out
      (block $a
        (block $b
          (block $c
            (block $dead
              br $a
              local.get 0
              (br_table $dead $c $b $out)
            )
            i32.const 10 local.set $r br $out
          )
          i32.const 20 local.set $r br $out
        )
        i32.const 30 local.set $r br $out
      )
    )
    local.get $r))
