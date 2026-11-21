;; Valid constant expression with three binary operations; the linker supports only one add-then-mul chain.
(module
  (type $items (array i32))
  (table 1 (ref null $items))
  (elem (table 0) (i32.const 0) (ref null $items)
    (array.new $items (i32.add (i32.mul (i32.add (i32.const 40) (i32.const 2)) (i32.const 3)) (i32.const 4)) (i32.const 1)))
)
