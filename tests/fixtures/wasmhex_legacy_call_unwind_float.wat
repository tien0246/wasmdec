(module
  (type $tag_f32 (func (param f32)))
  (type $tag_f64 (func (param f64)))
  (type $two_f32 (func (param f32 f32) (result f32)))
  (type $two_f64 (func (param f64 f64) (result f64)))
  (tag $signal_f32 (type $tag_f32))
  (tag $signal_f64 (type $tag_f64))

  (func $raise_f32 (export "raise_f32") (param f32)
    (local.get 0)
    (throw $signal_f32))

  ;; The result a1 + payload is reachable only through the throw edge: the
  ;; payload is the second parameter, so a lowering that drops the caught
  ;; payload computes the wrong value.
  (func (export "caught_payload_f32") (type $two_f32)
    (try
      (do
        (local.get 1)
        (call $raise_f32))
      (catch $signal_f32
        (local.get 0)
        (f32.add)
        (return)))
    unreachable)

  (func $raise_f64 (export "raise_f64") (param f64)
    (local.get 0)
    (throw $signal_f64))

  ;; The result a1 + payload is reachable only through the throw edge: the
  ;; payload is the second parameter, so a lowering that drops the caught
  ;; payload computes the wrong value.
  (func (export "caught_payload_f64") (type $two_f64)
    (try
      (do
        (local.get 1)
        (call $raise_f64))
      (catch $signal_f64
        (local.get 0)
        (f64.add)
        (return)))
    unreachable)
)
