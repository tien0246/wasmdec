(module
  (func (export "a") (param i32 i64 i64) (local.get 0) (drop) (local.get 1) (local.get 2) (i64.add) (drop))
  (func (export "b") (param i32 i32 i64 i32 i32 i64 i64) (result i32) (i32.const 0))
  (func (export "c") (param i64 i64 i64 i64) (local.get 0) (drop) (local.get 1) (drop) (local.get 2) (drop) (local.get 3) (drop)))
