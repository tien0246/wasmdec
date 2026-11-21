;; Valid constant initializer with two elements; the linker slice accepts computed initializers only at count 1.
(module
  (type $items (array i32))
  (table 1 (ref null $items))
  (elem (table 0) (i32.const 0) (ref null $items)
    (array.new_fixed $items 2
      (i32.add (i32.const 40) (i32.const 2))
      (i32.const 5)))
)
