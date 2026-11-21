(module
  (memory 1)
  (func (export "load8_lane") (param i32) (result i32)
    i32.const 400
    local.get 0
    i32.const 0
    v128.load
    v128.load8_lane offset=1 15
    v128.store8_lane offset=1 15
    i32.const 401
    i32.load8_u)
  (func (export "load16_lane") (param i32) (result i32)
    i32.const 410
    local.get 0
    i32.const 0
    v128.load
    v128.load16_lane offset=2 0
    v128.store16_lane offset=2 0
    i32.const 412
    i32.load16_u)
  (func (export "load32_lane") (param i32) (result i32)
    i32.const 420
    local.get 0
    i32.const 0
    v128.load
    v128.load32_lane offset=4 3
    v128.store32_lane offset=4 3
    i32.const 424
    i32.load)
  (func (export "load64_lane") (param i32) (result i64)
    i32.const 430
    local.get 0
    i32.const 0
    v128.load
    v128.load64_lane offset=8 1
    v128.store64_lane offset=8 1
    i32.const 438
    i64.load)
  (func (export "store8_lane") (param i32) (result i32)
    local.get 0
    i32.const 512
    v128.load
    v128.store8_lane offset=1 15
    local.get 0
    i32.load8_u offset=1)
  (func (export "store16_lane") (param i32) (result i32)
    local.get 0
    i32.const 512
    v128.load
    v128.store16_lane offset=2 7
    local.get 0
    i32.load16_u offset=2)
  (func (export "store32_lane") (param i32) (result i32)
    local.get 0
    i32.const 512
    v128.load
    v128.store32_lane offset=4 3
    local.get 0
    i32.load offset=4)
  (func (export "store64_lane") (param i32) (result i64)
    local.get 0
    i32.const 512
    v128.load
    v128.store64_lane offset=8 1
    local.get 0
    i64.load offset=8)
  (func (export "load8_lane_max_offset") (param i32) (result i32)
    local.get 0
    i32.const 0
    v128.load
    v128.load8_lane offset=4294967295 0
    drop
    i32.const 42)
  (func (export "init")
    i32.const 129
    i32.const 0x5a
    i32.store8
    i32.const 130
    i32.const 0x1234
    i32.store16
    i32.const 132
    i32.const 0x12345678
    i32.store
    i32.const 136
    i64.const 0x123456789abcdef0
    i64.store
    i32.const 512
    i32.const 0x04030201
    i32.store
    i32.const 516
    i32.const 0x08070605
    i32.store
    i32.const 520
    i32.const 0x0c0b0a09
    i32.store
    i32.const 524
    i32.const 0x100f0e0d
    i32.store))
