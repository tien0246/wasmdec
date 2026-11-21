(module
  (type $payload_type (func (param i32)))
  (tag $payload (type $payload_type))

  (func (export "typed_depth1") (result i32)
    (block (result i32)
      (block (result i32)
        (try_table (result i32) (catch $payload 1)
          (i32.const 42)
          (throw $payload)))))

  (func (export "catch_all_depth1")
    (block
      (block
        (try_table (catch_all 1)
          (i32.const 7)
          (throw $payload)))))

  (func (export "trap_not_caught")
    (block
      (block
        (try_table (catch_all 1)
          (unreachable))))))
