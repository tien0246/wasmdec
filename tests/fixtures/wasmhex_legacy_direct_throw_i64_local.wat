(module
  (type $payload_i64 (func (param i64)))
  (tag $wide (type $payload_i64))
  (func (export "local_i64") (param i64) (result i64)
    (try
      (do
        (local.get 0)
        (throw $wide))
      (catch $wide
        (return)))
    unreachable))
