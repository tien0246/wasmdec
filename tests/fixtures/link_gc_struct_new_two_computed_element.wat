;; WABT 1.0.42 cannot assemble GC struct.new ELEM initializers; the paired WASM is hand-encoded.
(module
  (type $item (struct (field i32) (field i64)))
  (table 1 (ref null $item))
  (elem (table 0) (i32.const 0) (ref null $item)
    (struct.new $item
      (i32.add (i32.const 40) (i32.const 2))
      (i64.mul (i64.const 30) (i64.const 3))))
)
