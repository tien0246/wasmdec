(module
  (func (export "dispatch") (param i32) (result i32)
    (local $r i32)
    (block $out
      (loop $loop
        (block $a
          (block $b
            (block $c
              (if (i32.ge_s (local.get 0) (i32.const 100))
                (then
                  local.get 0
                  i32.const 100
                  i32.sub
                  local.set 0))
              local.get 0
              (br_table $c $b $a $out)
            )
            i32.const 10 local.set $r br $out
          )
          i32.const 20 local.set $r br $out
        )
        i32.const 30 local.set $r br $out
      )
    )
    local.get $r))
