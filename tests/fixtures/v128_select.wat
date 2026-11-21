(module
  (func (export "untyped_lane0") (param i32 i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    local.get 2 select
    i32x4.extract_lane 0)
  (func (export "untyped_lane3") (param i32 i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    local.get 2 select
    i32x4.extract_lane 3)
  (func (export "typed_lane0") (param i32 i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    local.get 2 select (result v128)
    i32x4.extract_lane 0)
  (func (export "typed_lane3") (param i32 i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    local.get 2 select (result v128)
    i32x4.extract_lane 3))
