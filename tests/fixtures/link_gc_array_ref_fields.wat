;; WABT 1.0.42 cannot assemble typed GC element initializers; the paired binary has fixed-width ELEM/CODE relocations.
(module
  (type $fn (func))
  (type $refs (array (ref null $fn)))
  (func $target (type $fn))
  (func $run (type $fn)
    ref.func $target
    drop)
  (table 3 arrayref)
  (export "run" (func $run))
  (elem (table 0) (i32.const 0) arrayref
    (array.new $refs (ref.null $fn) (i32.const 1)))
  (elem (table 0) (i32.const 1) arrayref
    (array.new $refs (ref.func $target) (i32.const 1)))
  (elem (table 0) (i32.const 2) arrayref
    (array.new_fixed $refs 2 (ref.null $fn) (ref.func $target)))
)
