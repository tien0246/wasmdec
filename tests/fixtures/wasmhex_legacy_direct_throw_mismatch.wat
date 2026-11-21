(module
  (type $empty (func))
  (type $payload_i32 (func (param i32)))
  (tag $thrown (type $empty))
  (tag $caught (type $payload_i32))
  (func (export "mismatched") (result i32)
    (try
      (do (throw $thrown))
      (catch $caught
        (i32.const 1)
        (return)))
    unreachable))
