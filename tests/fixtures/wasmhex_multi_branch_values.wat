(module
  (type $pair (func (result i32 i64)))

  (func (export "wasm_br_pair") (result i32 i64)
    (block $done (type $pair)
      i32.const 10
      i64.const 20
      br $done
      i32.const 99
      i64.const 99))

  (func (export "wasm_br_if_pair") (param i32) (result i32 i64)
      (local i32 i64)
    (block $done (type $pair)
      i32.const 10
      i64.const 20
      local.get 0
      br_if $done
      local.set 2
      local.set 1
      i32.const 11
      i64.const 21))

  (func (export "wasm_br_table_pair") (param i32) (result i32 i64)
      (local i32 i64)
    (block $outer (type $pair)
      (block $inner (type $pair)
        i32.const 10
        i64.const 20
        local.get 0
        br_table $inner $outer)
      local.set 2
      local.set 1
      i32.const 11
      i64.const 21)))
