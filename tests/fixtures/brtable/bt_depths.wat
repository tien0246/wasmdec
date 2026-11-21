(module
  (func (export "dispatch") (param i32) (result i32)
    (local $r i32)
    (block $l0
      (block $l1
        (block $l2
          (block $l3
            (block $l4
              local.get 0
              (br_table $l4 $l3 $l2 $l1 $l0 $l4)
            )
            i32.const 4 local.set $r br $l0
          )
          i32.const 3 local.set $r br $l0
        )
        i32.const 2 local.set $r br $l0
      )
      i32.const 1 local.set $r
    )
    local.get $r))
