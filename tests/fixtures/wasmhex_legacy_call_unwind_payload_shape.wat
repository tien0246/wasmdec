(module
  (type $tag_i32 (func (param i32)))
  (type $result_i32 (func (result i32)))
  (tag $signal (type $tag_i32))

  (func $raise_constant
    (i32.const 42)
    (throw $signal))

  (func (export "unsupported_payload_shape") (type $result_i32)
    (try
      (do (call $raise_constant))
      (catch $signal
        (return)))
    unreachable))
