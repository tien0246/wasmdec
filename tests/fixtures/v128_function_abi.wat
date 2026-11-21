(module
  (func $identity (export "identity") (param v128) (result v128)
    local.get 0)
  (func $forward (export "forward") (param v128) (result v128)
    local.get 0 call $identity)
  (func $pair (export "pair") (param v128) (result v128 i64)
    local.get 0
    local.get 0 i64x2.extract_lane 1)
  (func $forward_pair (export "forward_pair") (param v128) (result v128 i64)
    local.get 0 call $pair)
  (func $pair_i32 (export "pair_i32") (param v128) (result v128 i32)
    local.get 0
    local.get 0 i64x2.extract_lane 1 i32.wrap_i64)
  (func $forward_pair_i32 (export "forward_pair_i32") (param v128) (result v128 i32)
    local.get 0 call $pair_i32)
  (func $return_if (export "return_if") (param v128 v128 i32) (result v128)
    local.get 0 local.get 2 br_if 0
    drop local.get 1)
  (func $return_if_pair (export "return_if_pair")
    (param v128 v128 i64 i32) (result v128 i64)
    local.get 0 local.get 2 local.get 3 br_if 0
    drop drop local.get 1 local.get 2)
  (func $make (export "make") (param i64) (result v128)
    local.get 0 i64x2.splat)
  (func $extract (export "extract") (param v128) (result i64)
    local.get 0 i64x2.extract_lane 0)
  (func $call_make (export "call_make") (param i64) (result i64)
    local.get 0 i64x2.splat call $identity i64x2.extract_lane 1))
