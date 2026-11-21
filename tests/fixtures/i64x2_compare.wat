(module
  (func (export "eq") (param i64 i64) (result i64)
    local.get 0
    i64x2.splat
    local.get 1
    i64x2.splat
    i64x2.eq
    i64x2.extract_lane 0)
  (func (export "ne") (param i64 i64) (result i64)
    local.get 0
    i64x2.splat
    local.get 1
    i64x2.splat
    i64x2.ne
    i64x2.extract_lane 0)
  (func (export "lt_s") (param i64 i64) (result i64)
    local.get 0
    i64x2.splat
    local.get 1
    i64x2.splat
    i64x2.lt_s
    i64x2.extract_lane 0)
  (func (export "gt_s") (param i64 i64) (result i64)
    local.get 0
    i64x2.splat
    local.get 1
    i64x2.splat
    i64x2.gt_s
    i64x2.extract_lane 0)
  (func (export "le_s") (param i64 i64) (result i64)
    local.get 0
    i64x2.splat
    local.get 1
    i64x2.splat
    i64x2.le_s
    i64x2.extract_lane 0)
  (func (export "ge_s") (param i64 i64) (result i64)
    local.get 0
    i64x2.splat
    local.get 1
    i64x2.splat
    i64x2.ge_s
    i64x2.extract_lane 0))
