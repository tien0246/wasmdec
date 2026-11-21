(module
  (type $tag_ref (func (param externref)))
  (type $externref_to_i32 (func (param externref) (result i32)))
  (tag $signal (type $tag_ref))

  (func $raise_ref (param externref)
    (local.get 0)
    (throw $signal))

  (func (export "ref_payload_unsupported") (type $externref_to_i32)
    (try
      (do
        (local.get 0)
        (call $raise_ref))
      (catch $signal
        (drop)
        (i32.const 1)
        (return)))
    unreachable))
