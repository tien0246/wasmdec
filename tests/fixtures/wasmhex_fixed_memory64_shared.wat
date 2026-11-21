(module
  (memory i64 1 1 shared)
  (func (export "load") (param $a i64) (result i32)
    local.get $a i32.load)
  (func (export "widen") (param $a i64) (result i32)
    local.get $a v128.load8x8_s drop i32.const 42)
)
