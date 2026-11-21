(module
  (memory 1)
  (table 1 funcref)
  (func $target)
  (global $sum (export "sum") i32
    (i32.add (i32.const 19) (i32.const 23)))
  (global $product64 (export "product64") i64
    (i64.mul (i64.const 3) (i64.const 4)))
  (data (i32.add (i32.const 0) (i32.const 1)) "x")
  (elem (i32.add (i32.const 0) (i32.const 0)) func $target))
