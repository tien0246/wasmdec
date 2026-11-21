(module
  (type $void (func))
  (type $result_i32 (func (result i32)))
  (tag $signal (type $void))

  (func $trap (export "trap") (type $void)
    unreachable)

  (func (export "trap_not_caught") (type $result_i32)
    (try
      (do
        (call $trap))
      (catch $signal
        (i32.const 7)
        (return)))
    unreachable))
