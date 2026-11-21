(module
  (type $payload_i64_pair (func (param i64 i64)))
  (tag $wide (type $payload_i64_pair))
  (func (export "multi_i64") (result i64)
    (try
      (do
        (i64.const 1)
        (i64.const 2)
        (throw $wide))
      (catch $wide
        (i64.const 3)
        (return)))
    unreachable))
