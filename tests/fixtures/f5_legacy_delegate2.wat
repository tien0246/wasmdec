(module
  (type $payload_type (func (param i32)))
  (tag $signal (type $payload_type))

  (func (export "delegate_two") (param i32)
    (try
      (do
        (try
          (do
            (try
              (do
                (try
                  (do
                    (if (local.get 0)
                      (then (i32.const 7) (throw $signal))))
                  (delegate 2)))
              (delegate 1)))
          (delegate 0))
        (nop))
      (catch $signal
        (drop))))
)
