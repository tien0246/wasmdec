;; Valid constant expression with two elements; the linker supports this computed form only for count 1.
(module
  (type $items (array i32))
  (table 1 (ref null $items))
  (elem (table 0) (i32.const 0) (ref null $items)
    (array.new_fixed $items 2 (i32.mul (i32.add (i32.const 40) (i32.const 2)) (i32.const 3)) (i32.const 5)))
)
