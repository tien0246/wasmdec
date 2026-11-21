(module
  (type $tag_i64 (func (param i64)))
  (type $two_i64 (func (param i64 i64) (result i64)))
  (tag $signal (type $tag_i64))

  (func $raise_i64 (export "raise_i64") (param i64)
    (local.get 0)
    (throw $signal))

  ;; The result a1 + payload is reachable only through the throw edge: the
  ;; payload is the second parameter, so a lowering that drops the caught
  ;; payload (returning either parameter alone) computes the wrong value.
  (func (export "caught_payload_i64") (type $two_i64)
    (try
      (do
        (local.get 1)
        (call $raise_i64))
      (catch $signal
        (local.get 0)
        (i64.add)
        (return)))
    unreachable)
)
