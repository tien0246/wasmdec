(module
  (func (export "add_sat_s") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.add_sat_s
    i16x8.extract_lane_s 0)
  (func (export "add_sat_u") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.add_sat_u
    i16x8.extract_lane_u 0)
  (func (export "sat_sub_s") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.sub_sat_s
    i16x8.extract_lane_s 0)
  (func (export "sat_sub_u") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.sub_sat_u
    i16x8.extract_lane_u 0))
