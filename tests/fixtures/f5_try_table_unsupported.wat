(module
  (type $payload_type (func (param i32)))
  (type $empty_type (func))
  (tag $payload (type $payload_type))
  (tag $other (type $payload_type))
  (tag $empty (type $empty_type))

  (func (export "wrong_tag") (result i32)
    (try_table (result i32) (catch $payload 0)
      (i32.const 7)
      (throw $other)))

  (func (export "nested_label") (result i32)
    (block (result i32)
      (try_table (result i32) (catch $payload 1)
        (i32.const 8)
        (throw $payload))))

  (func (export "catch_ref") (result i32 exnref)
    (try_table (result i32 exnref) (catch_ref $payload 0)
      (i32.const 9)
      (throw $payload)))

  (func (export "catch_all_not_final")
    (try_table (catch_all 0) (catch $empty 0)
      (throw $empty)))

  (func (export "empty_clauses")
    (try_table (nop)))

  (func (export "block_params") (param i32) (result i32)
    (local.get 0)
    (try_table (param i32) (result i32) (catch $payload 0)
      (throw $payload)))
)
