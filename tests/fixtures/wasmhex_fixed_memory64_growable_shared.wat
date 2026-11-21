(module
  (memory i64 1 2 shared)
  (func (export "load") (param $a i64) (result i32)
    local.get $a
    i32.load))
