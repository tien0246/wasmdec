;; WABT 1.0.42 cannot assemble GC constructors in element initializers; paired binary has a fixed-width type relocation.
(module
  (type $items (array i32))
  (table 1 (ref null $items))
  (elem (table 0) (i32.const 0) (ref null $items)
    (array.new_fixed $items 1 (i32.add (i32.const 40) (i32.const 2))))
)
