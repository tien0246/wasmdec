(module
  (type $multi_ref_fn (func (param i32) (result i32 externref externref)))
  (table $targets 8192 8192 funcref)
  (func $multi_ref (type $multi_ref_fn) (param i32)
    (result i32 externref externref)
    local.get 0
    ref.null extern
    ref.null extern)
  (elem (i32.const 0) $multi_ref)
  (func (export "dispatch_two_handles") (param $index i32) (param $value i32)
    (result i32 externref externref)
    local.get $value
    local.get $index
    call_indirect $targets (type $multi_ref_fn)))
