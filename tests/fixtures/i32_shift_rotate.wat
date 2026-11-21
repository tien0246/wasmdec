(module
  (func (export "shl") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.shl)
  (func (export "shr_s") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.shr_s)
  (func (export "shr_u") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.shr_u)
  (func (export "rotl") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.rotl)
  (func (export "rotr") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    i32.rotr))
