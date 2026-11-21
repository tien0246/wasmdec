;; WABT 1.0.42 cannot assemble typed GC element expressions; the test pairs this
;; expression with its core encoding and checks that linking preserves its type.
(module
  (type $items (array i32))
  (table $items_table 1 (ref null $items))
  (elem (table $items_table) (i32.const 0) (ref null $items)
    (array.new $items (i32.const 0) (i32.const 0)))
)
