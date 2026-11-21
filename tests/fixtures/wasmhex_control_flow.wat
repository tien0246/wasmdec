(module
  (func (export "wasm_if_else") (param i32) (result i32) (local i32)
    local.get 0
    if
      i32.const 10
      local.set 1
    else
      i32.const 20
      local.set 1
    end
    local.get 1)

  (func (export "wasm_block_branch") (param i32) (result i32) (local i32)
    block
      local.get 0
      br_if 0
      i32.const 5
      local.set 1
    end
    local.get 1)

  (func (export "wasm_loop_backedge") (param i32) (result i32) (local i32)
    i32.const 0
    local.set 1
    block
      loop
        local.get 0
        i32.eqz
        br_if 1
        local.get 1
        i32.const 1
        i32.add
        local.set 1
        local.get 0
        i32.const 1
        i32.sub
        local.set 0
        br 0
      end
    end
    local.get 1)

  (func (export "wasm_br_table") (param i32) (result i32) (local i32)
    block
      block
        local.get 0
        br_table 0 1
      end
      i32.const 11
      local.set 1
    end
    local.get 1))
