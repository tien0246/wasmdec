(module
  (global $sp (mut i32) (i32.const 1024))
  (memory 1)
  (data (i32.const 0) "hi")
  (func (export "sp_store") (param i32)
    global.get $sp
    local.get 0
    i32.store offset=16)
  (func (export "sp_roundtrip") (result i32)
    (local $t i32)
    global.get $sp
    local.set $t
    local.get $t
    i32.const 8
    i32.add
    global.set $sp
    global.get $sp)
  (func (export "brt") (param i32) (result i32)
    block
      block
        local.get 0
        br_table 0 1
      end
    end
    i32.const 7)
)
