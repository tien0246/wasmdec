(module
  (func (export "trap_then_dead") (result i32)
    unreachable
    i32.const 42))
