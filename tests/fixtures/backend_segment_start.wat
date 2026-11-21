(module
  (import "env" "callback" (func $callback))
  (memory 1)
  (data (i32.const 0) "a")
  (data $passive "p")
  (func $start
    i32.const 0 i32.const 0 i32.const 1 memory.init 0
    data.drop 0
    i32.const 0 i32.const 0 i32.const 1 memory.init $passive)
  (start $start)
  (func (export "unsafe")
    i32.const 0 i32.const 0 i32.const 1 memory.init $passive
    call $callback))
