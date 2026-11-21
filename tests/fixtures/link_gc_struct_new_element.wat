;; WABT cannot assemble typed GC element expressions; the paired binary uses core encodings.
(module
  (type $item (struct (field i32) (field i64)))
  (table $items 1 (ref null $item))
  (elem (table $items) (i32.const 0) (ref null $item)
    (struct.new $item (i32.const 17) (i64.const 23)))
)
