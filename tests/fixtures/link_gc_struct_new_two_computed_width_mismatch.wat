;; Invalid constant-expression result types: the first field is i64 but computes i32.
(module
  (type $item (struct (field i64) (field i32)))
  (table 1 (ref null $item))
  (elem (table 0) (i32.const 0) (ref null $item)
    (struct.new $item
      (i32.add (i32.const 40) (i32.const 2))
      (i64.add (i64.const 30) (i64.const 3))))
)
