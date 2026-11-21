(module
  (type $unary (func (param i32) (result i32)))
  (func (export "dispatch") (param $selector i32) (result i32)
    block $outer (result i32)
      block $inner (result i32)
        i32.const 99
        i32.const 7
        local.get $selector
        br_table $inner $outer
      end
      drop
      i32.const 11
    end
  )
)
