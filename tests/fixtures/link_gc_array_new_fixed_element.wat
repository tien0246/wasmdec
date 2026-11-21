;; WABT 1.0.42 cannot assemble typed GC element expressions; the paired binary uses core encodings.
(module
  (type $items (array i32))
  (table $items_table 1 (ref null $items))
  (elem (table $items_table) (i32.const 0) (ref null $items)
    (array.new_fixed $items 2 (i32.const 17) (i32.const 34)))
)
