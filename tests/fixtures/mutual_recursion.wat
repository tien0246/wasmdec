(module
  (func $even (export "even") (param $n i32) (result i32)
    local.get $n
    i32.eqz
    if (result i32)
      i32.const 1
    else
      local.get $n
      i32.const 1
      i32.sub
      call $odd
    end
  )
  (func $odd (export "odd") (param $n i32) (result i32)
    local.get $n
    i32.eqz
    if (result i32)
      i32.const 0
    else
      local.get $n
      i32.const 1
      i32.sub
      call $even
    end
  )
)
