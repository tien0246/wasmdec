(module
  (type $tag_i32 (func (param i32)))
  (type $result_i32 (func (result i32)))
  (type $i32_to_i32 (func (param i32) (result i32)))
  (tag $raised_signal (type $tag_i32))
  (tag $caught_signal (type $tag_i32))

  (func $raise_i32 (param i32)
    (local.get 0)
    (throw $raised_signal))

  (func (export "mismatched_payload_tag") (type $i32_to_i32)
    (try
      (do
        (local.get 0)
        (call $raise_i32))
      (catch $caught_signal
        (return)))
    unreachable))
