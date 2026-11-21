;; Valid extended constant expression used by the linker test.
(module
  (type $item (struct (field i32)))
  (table 1 (ref null $item))
  (elem (table 0) (i32.const 0) (ref null $item)
    (struct.new $item (i32.sub (i32.const 42) (i32.const 2))))
)
