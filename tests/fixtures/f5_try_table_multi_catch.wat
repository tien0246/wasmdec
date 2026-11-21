(module
  (type $pair (func (param i32 i64)))
  (type $empty (func))
  (tag $pair_a (type $pair))
  (tag $pair_b (type $pair))
  (tag $empty_a (type $empty))
  (tag $empty_b (type $empty))
  (tag $empty_other (type $empty))

  (func (export "throw_pair_a") (result i32 i64)
    (try_table (result i32 i64)
      (catch $pair_a 0)
      (catch $pair_b 0)
      (i32.const 41)
      (i64.const 1001)
      (throw $pair_a)))

  (func (export "throw_pair_b") (result i32 i64)
    (try_table (result i32 i64)
      (catch $pair_a 0)
      (catch $pair_b 0)
      (i32.const 42)
      (i64.const 1002)
      (throw $pair_b)))

  (func (export "throw_empty_tag")
    (try_table
      (catch $empty_a 0)
      (catch_all 0)
      (throw $empty_a)))

  (func (export "throw_empty_to_catch_all")
    (try_table
      (catch $empty_a 0)
      (catch $empty_b 0)
      (catch_all 0)
      (throw $empty_other)))
)
