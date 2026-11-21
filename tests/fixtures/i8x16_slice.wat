(module
  (func (export "add") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.add
    i8x16.extract_lane_s 0)
  (func (export "sub") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.sub
    i8x16.extract_lane_s 0)
  (func (export "add_sat_s") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.add_sat_s
    i8x16.extract_lane_s 0)
  (func (export "add_sat_u") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.add_sat_u
    i8x16.extract_lane_u 0)
  (func (export "sub_sat_s") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.sub_sat_s
    i8x16.extract_lane_s 0)
  (func (export "sub_sat_u") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.sub_sat_u
    i8x16.extract_lane_u 0)
  (func (export "min_s") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.min_s
    i8x16.extract_lane_s 0)
  (func (export "min_u") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.min_u
    i8x16.extract_lane_u 0)
  (func (export "max_s") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.max_s
    i8x16.extract_lane_s 0)
  (func (export "max_u") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.max_u
    i8x16.extract_lane_u 0)
  (func (export "eq") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.eq
    i8x16.extract_lane_u 0)
  (func (export "ne") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.ne
    i8x16.extract_lane_u 0)
  (func (export "lt_s") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.lt_s
    i8x16.extract_lane_u 0)
  (func (export "lt_u") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.lt_u
    i8x16.extract_lane_u 0)
  (func (export "gt_s") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.gt_s
    i8x16.extract_lane_u 0)
  (func (export "gt_u") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.gt_u
    i8x16.extract_lane_u 0)
  (func (export "le_s") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.le_s
    i8x16.extract_lane_u 0)
  (func (export "le_u") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.le_u
    i8x16.extract_lane_u 0)
  (func (export "ge_s") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.ge_s
    i8x16.extract_lane_u 0)
  (func (export "ge_u") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.ge_u
    i8x16.extract_lane_u 0)
  (func (export "all_true") (param i32) (result i32)
    local.get 0 i8x16.splat
    i8x16.all_true)
  (func (export "bitmask") (param i32) (result i32)
    local.get 0 i8x16.splat
    i8x16.bitmask)
  (func (export "abs") (param i32) (result i32)
    local.get 0 i8x16.splat
    i8x16.abs
    i8x16.extract_lane_u 15)
  (func (export "neg") (param i32) (result i32)
    local.get 0 i8x16.splat
    i8x16.neg
    i8x16.extract_lane_u 15)
  (func (export "extract_s") (param i32) (result i32)
    local.get 0 i8x16.splat
    i8x16.extract_lane_s 15)
  (func (export "extract_u") (param i32) (result i32)
    local.get 0 i8x16.splat
    i8x16.extract_lane_u 15)
  (func (export "replace") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1
    i8x16.replace_lane 7
    i8x16.extract_lane_u 7)
  (func (export "i16_splat_to_i8") (param i32) (result i32)
    local.get 0 i16x8.splat
    i8x16.extract_lane_u 5)
  (func (export "i8_splat_to_i16") (param i32) (result i32)
    local.get 0 i8x16.splat
    i16x8.extract_lane_u 3)
  (func (export "i32_splat_to_i8") (param i32) (result i32)
    local.get 0 i32x4.splat
    i8x16.extract_lane_u 15))
