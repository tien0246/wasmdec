(module
  (type $payload_i64 (func (param i64)))
  (tag $wide (type $payload_i64))
  (func (export "computed_i64") (result i64)
    (try
      (do
        (i64.const 1)
        (i64.const 2)
        (i64.add)
        (throw $wide))
      (catch $wide
        (return)))
    unreachable))
