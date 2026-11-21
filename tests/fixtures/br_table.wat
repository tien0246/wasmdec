(module
  (func (export "br_table") (param $selector i32) (result i32)
    (block $default
      (block $middle
        (block $inner
          local.get $selector
          br_table $inner $middle $default
        )
        i32.const 10
        return
      )
      i32.const 20
      return
    )
    i32.const 30
  )
)
