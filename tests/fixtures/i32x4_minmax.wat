(module
  (func (export "min_s") (param i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    i32x4.min_s
    i32x4.extract_lane 0)
  (func (export "min_u") (param i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    i32x4.min_u
    i32x4.extract_lane 0)
  (func (export "max_s") (param i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    i32x4.max_s
    i32x4.extract_lane 0)
  (func (export "max_u") (param i32 i32) (result i32)
    local.get 0 i32x4.splat
    local.get 1 i32x4.splat
    i32x4.max_u
    i32x4.extract_lane 0))
