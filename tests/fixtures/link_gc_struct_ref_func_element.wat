;; WABT 1.0.42 cannot assemble typed GC element initializers; the paired binary includes ELEM/CODE function and type relocations.
(module
  (type $fn (func))
  (type $item (struct (field i32) (field (ref null $fn))))
  (func $target (type $fn))
  (func $run (type $fn)
    ref.func $target
    drop)
  (table 1 (ref null $item))
  (export "run" (func $run))
  (elem (table 0) (i32.const 0) (ref null $item)
    (struct.new $item (i32.const 42) (ref.func $target)))
)
