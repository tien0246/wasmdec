(module
  (func (export "local_set_get_i16") (param i32) (result i32) (local v128)
    local.get 0
    i16x8.splat
    local.set 1
    local.get 1
    i32x4.extract_lane 1)
  (func (export "local_tee_i32") (param i32) (result i32) (local v128)
    local.get 0
    i32x4.splat
    local.tee 1
    i16x8.extract_lane_u 5)
  (func (export "local_zero_any") (result i32) (local v128)
    local.get 0
    v128.any_true))
