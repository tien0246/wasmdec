(module
  (func (export "wasm_if_result") (param i32) (result i32)
    local.get 0
    if (result i32)
      i32.const 10
    else
      i32.const 20
    end)

  (func (export "wasm_if_result_prefix") (param i32) (result i32)
    i32.const 7
    local.get 0
    if (result i32)
      i32.const 10
    else
      i32.const 20
    end
    i32.add)

  (func (export "wasm_br_if_result") (param i32) (result i32)
    block (result i32)
      i32.const 11
      local.get 0
      br_if 0
      i32.const 22
      i32.add
    end)

  (func (export "wasm_br_result") (result i32)
    block (result i32)
      i32.const 42
      br 0
      i32.const 99
    end)

  (func (export "wasm_br_table_result") (param i32) (result i32)
    block (result i32)
      block (result i32)
        i32.const 10
        local.get 0
        br_table 0 1
      end
      i32.const 30
      i32.add
    end)

  (func (export "wasm_i64_br_if_result") (param i32) (result i64)
    block (result i64)
      i64.const 44
      local.get 0
      br_if 0
      i64.const 2
      i64.add
    end)

  (func (export "wasm_f32_if_result") (param i32) (result f32)
    local.get 0
    if (result f32)
      f32.const 1.5
    else
      f32.const 2.5
    end)

  (func (export "wasm_externref_br_if_result")
    (param i32 externref) (result externref)
    block (result externref)
      local.get 1
      local.get 0
      br_if 0
    end)
)
