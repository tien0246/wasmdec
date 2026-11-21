;; Valid constant expression; the linker supports only one binary op per field.
(module
  (type $item (struct (field i32) (field i64)))
  (table 1 (ref null $item))
  (elem (table 0) (i32.const 0) (ref null $item)
    (struct.new $item
      (i32.add (i32.mul (i32.const 40) (i32.const 2)) (i32.const 3))
      (i64.add (i64.const 30) (i64.const 3))))
)
