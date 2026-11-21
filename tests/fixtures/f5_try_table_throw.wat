(module
  (type $payload_type (func (param i32)))
  (tag $payload (type $payload_type))

  (func (export "throw_tag") (result i32)
    (try_table (result i32) (catch $payload 0)
      (i32.const 42)
      (throw $payload)))

  (func (export "catch_all")
    (try_table (catch_all 0)
      (i32.const 90)
      (throw $payload)))
)
