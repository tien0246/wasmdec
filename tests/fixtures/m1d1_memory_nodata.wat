(module
  (memory 1)
  (func (export "brt2") (param i32) (result i32)
    block
      block
        local.get 0
        br_table 0 1
      end
    end
    i32.const 7)
  (func (export "brt3") (param i32) (result i32)
    block
      block
        block
          local.get 0
          br_table 0 1 2
        end
      end
    end
    i32.const 7)
)
