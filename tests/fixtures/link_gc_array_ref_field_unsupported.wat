;; The paired binary is valid, but nested array constructors in ELEM initializers remain unsupported by the linker.
(module
  (type $inner (array i32))
  (type $outer (array (ref null $inner)))
  (table 1 arrayref)
  (elem (table 0) (i32.const 0) arrayref
    (array.new $outer
      (array.new_default $inner (i32.const 0))
      (i32.const 1)))
)
