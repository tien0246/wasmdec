(module
  (import "env" "run" (func $run (param i32 i32) (result i32)))
  (import "env" "memory" (memory 1))
  (table 2 funcref)
  (elem (i32.const 0) $callee_a $callee_b)
  (type $runner (func (param i32 i32) (result i32)))
  (func $callee_a (type $runner)
    local.get 0
    local.get 1
    i32.add)
  (func $callee_b (type $runner)
    local.get 0
    local.get 1
    i32.mul)
  (func (export "a1_first_arg_is_temp") (param i32 i32) (result i32)
    i32.const 1024
    i32.load
    local.get 1
    local.get 1
    call_indirect (type $runner))
  (func (export "a1_simple") (param i32 i32) (result i32)
    local.get 0
    local.get 1
    local.get 1
    call_indirect (type $runner))
  (data (i32.const 1024) "\11\00\00\00")
)
