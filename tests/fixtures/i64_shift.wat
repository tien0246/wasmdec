(module
  (func (export "shift") (param i64 i64) (result i64)
    local.get 0
    local.get 1
    i64.shl)
  (func (export "shr_s") (param i64 i64) (result i64)
    local.get 0
    local.get 1
    i64.shr_s)
  (func (export "shr_u") (param i64 i64) (result i64)
    local.get 0
    local.get 1
    i64.shr_u)
  (func (export "rotl") (param i64 i64) (result i64)
    local.get 0
    local.get 1
    i64.rotl)
  (func (export "rotr") (param i64 i64) (result i64)
    local.get 0
    local.get 1
    i64.rotr))
