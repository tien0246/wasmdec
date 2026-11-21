(module
  (func (export "br0") (result i32)
    block
      br 0
    end
    i32.const 7)
  (func (export "brt1") (param i32) (result i32)
    block
      local.get 0
      br_table 0
    end
    i32.const 7)
  (func (export "brt2") (param i32) (result i32)
    block
      block
        local.get 0
        br_table 0 1
      end
    end
    i32.const 7)
  (func (export "br_if0") (param i32) (result i32)
    block
      local.get 0
      br_if 0
    end
    i32.const 7)
)
