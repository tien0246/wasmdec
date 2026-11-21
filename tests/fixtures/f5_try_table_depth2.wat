(module
  (type $payload_type (func (param i32)))
  (tag $payload (type $payload_type))

  (func (export "typed_depth2") (result i64 i32)
    (i64.const 9)
    (block (result i32)
      (block (result i32)
        (block (result i32)
          (try_table (result i32) (catch $payload 2)
            (i32.const 42)
            (throw $payload))))))

  (func (export "catch_all_depth2") (result i64)
    (i64.const 9)
    (block
      (block
        (block
          (try_table (catch_all 2)
            (i32.const 7)
            (throw $payload))))))

  (func (export "trap_not_caught_depth2")
    (block
      (block
        (block
          (try_table (catch_all 2)
            (unreachable)))))))
