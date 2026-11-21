;; WABT 1.0.42 cannot assemble GC array constructors in element initializers; the paired binary carries a fixed-width type relocation.
(module
  (type $unused (array i64))
  (type $items (array i32))
  (table 1 (ref null $items))
  (elem (table 0) (i32.const 0) (ref null $items)
    (array.new $items (i32.add (i32.const 40) (i32.const 2)) (i32.const 1)))
)
