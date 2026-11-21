;; Hand-encoded negative fixture: second-field i32 expression does not match its i64 field.
(module
  (type $item (struct (field i32) (field i64)))
  (table $items 1 (ref null $item))
  (elem (table $items) (i32.const 0) (ref null $item)
    (struct.new $item (i32.const 17)
      (i32.add (i32.const 40) (i32.const 2))))
)
