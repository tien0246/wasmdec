(module
  (func (export "i8_popcnt") (param i32) (result i32)
    local.get 0 i8x16.splat
    i8x16.popcnt
    i8x16.extract_lane_u 15)
  (func (export "i8_avgr_u") (param i32 i32) (result i32)
    local.get 0 i8x16.splat
    local.get 1 i8x16.splat
    i8x16.avgr_u
    i8x16.extract_lane_u 15)
  (func (export "i16_avgr_u") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.avgr_u
    i16x8.extract_lane_u 7)
  (func (export "i16_q15mulr_sat_s") (param i32 i32) (result i32)
    local.get 0 i16x8.splat
    local.get 1 i16x8.splat
    i16x8.q15mulr_sat_s
    i16x8.extract_lane_s 7))
