;; WABT 1.0.42 cannot assemble struct.new ELEM initializers; the paired WASM is hand-encoded.
(module
  (type $item (struct (field i32) (field i64)))
  (table $items 1 (ref null $item))
  (elem (table $items) (i32.const 0) (ref null $item)
    (struct.new $item (i32.const 17) (i64.add (i64.const 40) (i64.const 2))))
)
