;; WABT 1.0.42 cannot assemble i31ref tables or i31.new constant expressions.
;; The paired binary uses core GC encodings: table i31ref, element (ref i31), i31.new 42.
(module
  (table $items 1 i31ref)
  (export "items" (table $items))
  (elem (table $items) (i32.const 0) (ref i31)
    (i31.new (i32.const 42)))
)
