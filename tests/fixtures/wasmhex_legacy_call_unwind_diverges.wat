(module
  (type $void (func))
  (type $result_i32 (func (result i32)))
  (tag $signal (type $void))
  (global $condition (mut i32) (i32.const 0))

  (func $loop_or_throw (type $void)
    (global.get $condition)
    (if
      (then
        (loop
          (br 0)))
      (else
        (throw $signal))))

  (func (export "diverges_not_caught") (type $result_i32)
    (try
      (do
        (call $loop_or_throw))
      (catch $signal
        (i32.const 7)
        (return)))
    unreachable))
