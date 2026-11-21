(module
  (memory 1)

  (func (export "init")
    i32.const 128 i64.const 0x12345678efcdab89 i64.store
    i32.const 160 f32.const 1.5 f32.store
    i32.const 168 f64.const 2.25 f64.store)

  (func (export "load_i32") (param i32) (result i32)
    local.get 0 i32.load offset=128)
  (func (export "load_i64") (param i32) (result i64)
    local.get 0 i64.load offset=128)
  (func (export "load_f32") (param i32) (result f32)
    local.get 0 f32.load offset=160)
  (func (export "load_f64") (param i32) (result f64)
    local.get 0 f64.load offset=168)
  (func (export "load_i32_8s") (param i32) (result i32)
    local.get 0 i32.load8_s offset=128)
  (func (export "load_i32_8u") (param i32) (result i32)
    local.get 0 i32.load8_u offset=128)
  (func (export "load_i32_16s") (param i32) (result i32)
    local.get 0 i32.load16_s offset=128)
  (func (export "load_i32_16u") (param i32) (result i32)
    local.get 0 i32.load16_u offset=128)
  (func (export "load_i64_8s") (param i32) (result i64)
    local.get 0 i64.load8_s offset=128)
  (func (export "load_i64_8u") (param i32) (result i64)
    local.get 0 i64.load8_u offset=128)
  (func (export "load_i64_16s") (param i32) (result i64)
    local.get 0 i64.load16_s offset=128)
  (func (export "load_i64_16u") (param i32) (result i64)
    local.get 0 i64.load16_u offset=128)
  (func (export "load_i64_32s") (param i32) (result i64)
    local.get 0 i64.load32_s offset=128)
  (func (export "load_i64_32u") (param i32) (result i64)
    local.get 0 i64.load32_u offset=128)

  (func (export "store_i32") (param i32 i32) (result i32)
    local.get 0 local.get 1 i32.store offset=256
    local.get 0 i32.load offset=256)
  (func (export "store_i64") (param i32 i64) (result i64)
    local.get 0 local.get 1 i64.store offset=256
    local.get 0 i64.load offset=256)
  (func (export "store_f32") (param i32 f32) (result f32)
    local.get 0 local.get 1 f32.store offset=256
    local.get 0 f32.load offset=256)
  (func (export "store_f64") (param i32 f64) (result f64)
    local.get 0 local.get 1 f64.store offset=256
    local.get 0 f64.load offset=256)
  (func (export "store_i32_8") (param i32 i32) (result i32)
    local.get 0 local.get 1 i32.store8 offset=256
    local.get 0 i32.load8_u offset=256)
  (func (export "store_i32_16") (param i32 i32) (result i32)
    local.get 0 local.get 1 i32.store16 offset=256
    local.get 0 i32.load16_u offset=256)
  (func (export "store_i64_8") (param i32 i64) (result i64)
    local.get 0 local.get 1 i64.store8 offset=256
    local.get 0 i64.load8_u offset=256)
  (func (export "store_i64_16") (param i32 i64) (result i64)
    local.get 0 local.get 1 i64.store16 offset=256
    local.get 0 i64.load16_u offset=256)
  (func (export "store_i64_32") (param i32 i64) (result i64)
    local.get 0 local.get 1 i64.store32 offset=256
    local.get 0 i64.load32_u offset=256)

  (func (export "load_max_offset") (param i32) (result i32)
    local.get 0 i32.load offset=4294967295)
)
