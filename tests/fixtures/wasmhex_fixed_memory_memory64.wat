(module
  (memory i64 1)
  (func (export "load") (param i64) (result i32)
    local.get 0
    v128.load
    drop
    i32.const 42)
  (func (export "widen") (param i64) (result i32)
    local.get 0 v128.load8x8_s drop
    local.get 0 v128.load8x8_u drop
    local.get 0 v128.load16x4_s drop
    local.get 0 v128.load16x4_u drop
    local.get 0 v128.load32x2_s drop
    local.get 0 v128.load32x2_u drop
    i32.const 42))
