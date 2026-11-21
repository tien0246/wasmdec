(module
  (type $payload_i64 (func (param i64)))
  (tag $wide (type $payload_i64))
  (func (export "non_entry_i64") (result i64)
    (block
      (try
        (do
          (i64.const 1)
          (throw $wide))
        (catch $wide
          (return))))
    unreachable))
