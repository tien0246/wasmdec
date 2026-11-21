(module
  (type $void (func))
  (type $result_i32 (func (result i32)))
  (type $two_i32 (func (param i32 i32) (result i32)))
  (type $tag_i32 (func (param i32)))
  (tag $signal (type $void))
  (tag $signal_i32 (type $tag_i32))

  (func $raise (export "raise_tag") (type $void)
    (throw $signal))

  (func (export "caught_call") (type $result_i32)
    (try
      (do
        (call $raise))
      (catch $signal
        (i32.const 7)
        (return)))
    unreachable)

  (func $raise_i32 (export "raise_i32") (param i32)
    (local.get 0)
    (throw $signal_i32))

  ;; The result 3 * (a1 + payload) is reachable only through the throw edge:
  ;; the payload is the second parameter, and the catch-only transform folds in
  ;; the first parameter, so a lowering that drops the caught payload (returning
  ;; either parameter alone) computes the wrong value.
  (func (export "caught_payload") (type $two_i32)
    (try
      (do
        (local.get 1)
        (call $raise_i32))
      (catch $signal_i32
        (local.get 0)
        (i32.add)
        (i32.const 3)
        (i32.mul)
        (return)))
    unreachable)
)
