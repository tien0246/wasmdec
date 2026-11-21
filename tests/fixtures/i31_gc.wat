;; WABT 1.0.42 cannot assemble these GC instructions; the paired binary uses their core opcodes.
(module
  (func (export "signed") (param $value i32) (result i32)
    (local $reference (ref i31))
    local.get $value
    ref.i31
    local.tee $reference
    i31.get_s)
  (func (export "unsigned") (param $value i32) (result i32)
    local.get $value
    ref.i31
    i31.get_u)
  (func (export "null_traps") (result i32)
    ref.null i31
    i31.get_s))
