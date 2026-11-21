(module
  (func (export "large_br_table") (param $selector i32) (result i32)
    (local $value i32)
    (block $skip
      (block $return
        (local.set $value (i32.const 7))
        (local.get $selector)
        (br_table $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $skip $return)
      )
      (local.set $value (i32.const 11))
    )
    (local.get $value)
  )
)
