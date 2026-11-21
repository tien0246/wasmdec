(module
  (func (export "add") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.add
    i16x8.extract_lane_s 0)
  (func (export "sub") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.sub
    i16x8.extract_lane_s 0)
  (func (export "mul") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.mul
    i16x8.extract_lane_s 0)
  (func (export "min_s") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.min_s
    i16x8.extract_lane_s 0)
  (func (export "min_u") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.min_u
    i16x8.extract_lane_u 0)
  (func (export "max_s") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.max_s
    i16x8.extract_lane_s 0)
  (func (export "max_u") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.max_u
    i16x8.extract_lane_u 0)
  (func (export "eq") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.eq
    i16x8.extract_lane_u 0)
  (func (export "ne") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.ne
    i16x8.extract_lane_u 0)
  (func (export "lt_s") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.lt_s
    i16x8.extract_lane_u 0)
  (func (export "lt_u") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.lt_u
    i16x8.extract_lane_u 0)
  (func (export "gt_s") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.gt_s
    i16x8.extract_lane_u 0)
  (func (export "gt_u") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.gt_u
    i16x8.extract_lane_u 0)
  (func (export "le_s") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.le_s
    i16x8.extract_lane_u 0)
  (func (export "le_u") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.le_u
    i16x8.extract_lane_u 0)
  (func (export "ge_s") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.ge_s
    i16x8.extract_lane_u 0)
  (func (export "ge_u") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.ge_u
    i16x8.extract_lane_u 0)
  (func (export "all_true") (param i32) (result i32)
    local.get 0 i16x8.splat
    i16x8.all_true)
  (func (export "bitmask") (param i32) (result i32)
    local.get 0 i16x8.splat
    i16x8.bitmask)
  (func (export "extract_s") (param i32) (result i32)
    local.get 0 i16x8.splat
    i16x8.extract_lane_s 7)
  (func (export "extract_u") (param i32) (result i32)
    local.get 0 i16x8.splat
    i16x8.extract_lane_u 7)
  (func (export "replace") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1
    i16x8.replace_lane 3
    i16x8.extract_lane_u 3)
)
