(module
  (func (export "dispatch") (param i32) (result i32)
    (block $out (result i32)
      (block $a (result i32)
        (block $b (result i32)
          (block $c (result i32)
            (i32.add (local.get 0) (i32.const 100))
            (local.get 0)
            (br_table $c $b $a $out)
          )
          i32.const 1000 i32.add
        )
        i32.const 2000 i32.add
      )
      i32.const 3000 i32.add
    )))
