(module
  (type $payload_type (func (param i32)))
  (import "env" "callee" (func $callee))
  (tag $payload (type $payload_type))

  (func (export "uncaught")
    (i32.const 1)
    (throw $payload))

  (func (export "call_inside_try")
    (try
      (do
        (call $callee))
      (catch $payload
        (drop)))))
