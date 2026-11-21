(module
  (type $tag_pair (func (param i32 i32)))
  (type $result_i32 (func (result i32)))
  (tag $signal (type $tag_pair))

  (func $raise_pair (param i32 i32)
    (local.get 0)
    (local.get 1)
    (throw $signal))

  (func (export "unsupported_payload") (type $result_i32)
    (try
      (do
        (i32.const 42)
        (i32.const 7)
        (call $raise_pair))
      (catch $signal
        (drop)
        (drop)
        (i32.const 1)
        (return)))
    unreachable))
